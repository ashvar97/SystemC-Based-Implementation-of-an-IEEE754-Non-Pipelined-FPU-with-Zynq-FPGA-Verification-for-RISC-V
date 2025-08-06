//==============================================================================
//
// Module: ieee754mult_pipelined (3-stage pipeline)
//
module ieee754mult_pipelined
(
    input logic clk,
    input logic reset,
    input logic [31:0] A,
    input logic [31:0] B,
    input logic valid_in,
    output logic [31:0] result,
    output logic valid_out
);

// Stage 1: Extraction pipeline registers
logic [31:0] A_reg1, B_reg1;
logic valid_stage1;
logic A_sign_stage1, B_sign_stage1;
logic [7:0] A_Exponent_stage1, B_Exponent_stage1;
logic [23:0] A_Mantissa_stage1, B_Mantissa_stage1;

// Stage 2: Multiplication pipeline registers
logic valid_stage2;
logic Sign_stage2;
logic [7:0] A_Exponent_stage2, B_Exponent_stage2;
logic [7:0] Temp_Exponent_stage2;
logic [47:0] Temp_Mantissa_stage2;

// Stage 3: Normalization (output stage)
// result and valid_out are the final outputs

//------------------------------------------------------------------------------
// Stage 1: Extraction
//------------------------------------------------------------------------------

// Pipeline registers for Stage 1
always_ff @(posedge clk or posedge reset) begin
    if (reset) begin
        A_reg1 <= 32'h0;
        B_reg1 <= 32'h0;
        valid_stage1 <= 1'b0;
    end else begin
        A_reg1 <= A;
        B_reg1 <= B;
        valid_stage1 <= valid_in;
    end
end

// Extraction logic (combinational)
always_comb begin
    // Extract A
    A_sign_stage1 = A_reg1[31];
    A_Exponent_stage1 = A_reg1[30:23];
    A_Mantissa_stage1 = (A_Exponent_stage1 == 8'h00) ? {1'b0, A_reg1[22:0]} : {1'b1, A_reg1[22:0]};
    
    // Extract B
    B_sign_stage1 = B_reg1[31];
    B_Exponent_stage1 = B_reg1[30:23];
    B_Mantissa_stage1 = (B_Exponent_stage1 == 8'h00) ? {1'b0, B_reg1[22:0]} : {1'b1, B_reg1[22:0]};
end

//------------------------------------------------------------------------------
// Stage 2: Multiplication
//------------------------------------------------------------------------------

// Pipeline registers for Stage 2
always_ff @(posedge clk or posedge reset) begin
    if (reset) begin
        valid_stage2 <= 1'b0;
        A_Exponent_stage2 <= 8'h0;
        B_Exponent_stage2 <= 8'h0;
        Sign_stage2 <= 1'b0;
        Temp_Exponent_stage2 <= 8'h0;
        Temp_Mantissa_stage2 <= 48'h0;
    end else begin
        valid_stage2 <= valid_stage1;
        A_Exponent_stage2 <= A_Exponent_stage1;
        B_Exponent_stage2 <= B_Exponent_stage1;
        
        // Multiplication logic
        Sign_stage2 <= A_sign_stage1 ^ B_sign_stage1;
        Temp_Mantissa_stage2 <= A_Mantissa_stage1 * B_Mantissa_stage1;
        
        // Handle exponent addition with special cases
        if (A_Exponent_stage1 == 8'h00 || B_Exponent_stage1 == 8'h00) begin
            // Zero case
            Temp_Exponent_stage2 <= 8'h00;
        end else if (A_Exponent_stage1 == 8'hFF || B_Exponent_stage1 == 8'hFF) begin
            // Infinity/NaN case
            Temp_Exponent_stage2 <= 8'hFF;
        end else begin
            // Normal case: Add exponents and subtract bias
            Temp_Exponent_stage2 <= A_Exponent_stage1 + B_Exponent_stage1 - 8'd127;
        end
    end
end

//------------------------------------------------------------------------------
// Stage 3: Normalization
//------------------------------------------------------------------------------

// Normalization logic variables (declared at module level)
logic [22:0] final_mantissa;
logic [7:0] final_exponent;

// Combinational logic for normalization
always_comb begin
    // Default values
    final_mantissa = 23'h0;
    final_exponent = 8'h00;
    
    if (Temp_Mantissa_stage2[47]) begin
        // Normalized case: MSB is 1, shift right
        final_mantissa = Temp_Mantissa_stage2[46:24];
        final_exponent = Temp_Exponent_stage2 + 8'd1;
    end else if (Temp_Mantissa_stage2[46]) begin
        // Already normalized
        final_mantissa = Temp_Mantissa_stage2[45:23];
        final_exponent = Temp_Exponent_stage2;
    end else begin
        // Result is too small, might underflow
        final_mantissa = 23'h0;
        final_exponent = 8'h00;
    end
end

// Pipeline registers for Stage 3 (output stage)
always_ff @(posedge clk or posedge reset) begin
    if (reset) begin
        result <= 32'h0;
        valid_out <= 1'b0;
    end else begin
        valid_out <= valid_stage2;
        
        if (!valid_stage2) begin
            result <= 32'h0;
        end else if (A_Exponent_stage2 == 8'h00 || B_Exponent_stage2 == 8'h00) begin
            // Zero result
            result <= 32'h00000000;
        end else if (A_Exponent_stage2 == 8'hFF) begin
            // A is infinity or NaN
            if (B_Exponent_stage2 == 8'h00) begin
                // Infinity * Zero = NaN
                result <= 32'h7FC00000;
            end else begin
                // Infinity * finite = Infinity
                result <= {Sign_stage2, 8'hFF, 23'h0};
            end
        end else if (B_Exponent_stage2 == 8'hFF) begin
            // B is infinity or NaN
            if (A_Exponent_stage2 == 8'h00) begin
                // Zero * Infinity = NaN
                result <= 32'h7FC00000;
            end else begin
                // Finite * Infinity = Infinity
                result <= {Sign_stage2, 8'hFF, 23'h0};
            end
        end else begin
            // Check for overflow/underflow
            if (final_exponent >= 8'hFF) begin
                // Overflow to infinity
                result <= {Sign_stage2, 8'hFF, 23'h0};
            end else if (final_exponent == 8'h00) begin
                // Underflow to zero
                result <= {Sign_stage2, 8'h00, 23'h0};
            end else begin
                // Normal result
                result <= {Sign_stage2, final_exponent, final_mantissa};
            end
        end
    end
end

endmodule

//==============================================================================
//
// Testbench for Pipelined IEEE754 Multiplication Module
//
`timescale 1ns/1ps

module ieee754_mult_tb;

    // Clock and reset
    logic clk;
    logic reset;
    
    // DUT inputs/outputs
    logic [31:0] A, B;
    logic valid_in;
    logic [31:0] result;
    logic valid_out;
    
    // Test control
    int test_count = 0;
    int pass_count = 0;
    int fail_count = 0;
    
    // Pipeline control - using simple arrays instead of structs
    real expected_real_queue[$];
    logic [31:0] expected_hex_queue[$];
    string test_name_queue[$];
    int test_id_queue[$];
    
    // Clock generation
    initial begin
        clk = 0;
        forever #5 clk = ~clk; // 100MHz clock
    end
    
    // DUT instantiation
    ieee754mult_pipelined dut (
        .clk(clk),
        .reset(reset),
        .A(A),
        .B(B),
        .valid_in(valid_in),
        .result(result),
        .valid_out(valid_out)
    );
    
    // Helper function to convert real to IEEE754
    function automatic logic [31:0] real_to_ieee754(real r);
        real abs_r;
        logic sign;
        int exp;
        logic [22:0] mantissa;
        real normalized;
        
        if (r == 0.0) return 32'h00000000;
        
        sign = (r < 0.0);
        abs_r = sign ? -r : r;
        
        // Handle special cases
        if (abs_r != abs_r) return 32'h7FC00000; // NaN
        if (abs_r > 3.4e38) return {sign, 8'hFF, 23'h0}; // Infinity
        
        // Find exponent
        exp = 0;
        normalized = abs_r;
        while (normalized >= 2.0) begin
            normalized = normalized / 2.0;
            exp = exp + 1;
        end
        while (normalized < 1.0) begin
            normalized = normalized * 2.0;
            exp = exp - 1;
        end
        
        exp = exp + 127; // Add bias
        
        // Extract mantissa (remove implicit 1)
        normalized = normalized - 1.0;
        mantissa = int'(normalized * (2**23));
        
        return {sign, exp[7:0], mantissa};
    endfunction
    
    // Helper function to convert IEEE754 to real
    function automatic real ieee754_to_real(logic [31:0] ieee);
        logic sign;
        logic [7:0] exp;
        logic [22:0] frac;
        real result_real;
        
        sign = ieee[31];
        exp = ieee[30:23];
        frac = ieee[22:0];
        
        // Special cases
        if (exp == 8'hFF) begin
            if (frac != 0) return 0.0; // NaN - return 0 for comparison
            return sign ? -1e38 : 1e38; // Infinity approximation
        end
        
        if (exp == 0 && frac == 0) return 0.0; // Zero
        
        // Normal numbers
        result_real = (1.0 + real'(frac) / (2**23)) * (2**(int'(exp) - 127));
        return sign ? -result_real : result_real;
    endfunction
    
    // Task to send test case to pipeline
    task automatic send_test_case(
        input real a_real,
        input real b_real,
        input string test_name
    );
        logic [31:0] a_hex, b_hex;
        real expected_real;
        logic [31:0] expected_hex;
        
        test_count++;
        
        a_hex = real_to_ieee754(a_real);
        b_hex = real_to_ieee754(b_real);
        expected_real = a_real * b_real;
        expected_hex = real_to_ieee754(expected_real);
        
        $display("Test %0d: %s", test_count, test_name);
        $display("  A = %f (0x%08h)", a_real, a_hex);
        $display("  B = %f (0x%08h)", b_real, b_hex);
        $display("  Expected = %f (0x%08h)", expected_real, expected_hex);
        
        // Queue expected result for pipeline
        expected_real_queue.push_back(expected_real);
        expected_hex_queue.push_back(expected_hex);
        test_name_queue.push_back(test_name);
        test_id_queue.push_back(test_count);
        
        // Send to pipeline
        @(posedge clk);
        A <= a_hex;
        B <= b_hex;
        valid_in <= 1'b1;
        
        @(posedge clk);
        valid_in <= 1'b0;
        A <= 32'h0;
        B <= 32'h0;
    endtask
    
    // Task to check pipeline result
    task automatic check_result(
        input real expected_real,
        input logic [31:0] expected_hex,
        input string test_name,
        input int test_id
    );
        real result_real;
        real error;
        real tolerance = 1e-5;
        logic pass;
        
        result_real = ieee754_to_real(result);
        
        // Handle special cases
        if (expected_real != expected_real) begin // NaN
            pass = (result_real != result_real) || (result == 32'h7FC00000);
        end else if (expected_real > 1e37 || expected_real < -1e37) begin // Infinity
            pass = (result == expected_hex) || (result == 32'h7F800000) || (result == 32'hFF800000);
        end else if (expected_real == 0.0) begin // Zero
            pass = ((result & 32'h7FFFFFFF) == 0);
        end else begin
            if (expected_real != 0.0) begin
                error = (result_real - expected_real) / expected_real;
                if (error < 0) error = -error;
            end else begin
                error = (result_real < 0) ? -result_real : result_real;
            end
            pass = (error < tolerance);
        end
        
        $display("Test %0d Result: %s", test_id, test_name);
        $display("  Got = %f (0x%08h)", result_real, result);
        
        if (pass) begin
            $display("  PASS");
            pass_count++;
        end else begin
            $display("  FAIL - Error: %e", error);
            fail_count++;
        end
        $display("");
    endtask
    
    // Monitor for pipeline output
    always_ff @(posedge clk) begin
        if (valid_out && expected_real_queue.size() > 0) begin
            real expected_real;
            logic [31:0] expected_hex;
            string test_name;
            int test_id;
            
            expected_real = expected_real_queue.pop_front();
            expected_hex = expected_hex_queue.pop_front();
            test_name = test_name_queue.pop_front();
            test_id = test_id_queue.pop_front();
            
            check_result(expected_real, expected_hex, test_name, test_id);
        end else if (valid_out && expected_real_queue.size() == 0) begin
            $display("WARNING: Unexpected valid_out at time %0t", $time);
        end
    end
    
    // Main test sequence
    initial begin
        $display("=== IEEE 754 Pipelined Multiplication Testbench ===\n");
        
        // Initialize
        reset = 1'b1;
        valid_in = 1'b0;
        A = 32'h0;
        B = 32'h0;
        
        // Reset sequence
        repeat(5) @(posedge clk);
        reset = 1'b0;
        repeat(3) @(posedge clk);
        
        $display("Starting 5 test cases for pipelined multiplication...\n");
        
        // Test Case 1: Simple multiplication
        send_test_case(5.0, 2.0, "Simple Multiplication: 5.0 * 2.0");
        
        // Test Case 2: Fractional multiplication
        send_test_case(2.5, 3.0, "Fractional Multiplication: 2.5 * 3.0");
        
        // Test Case 3: Large number multiplication
        send_test_case(12.5, 8.0, "Large Numbers: 12.5 * 8.0");
        
        // Test Case 4: Multiplication by zero
        send_test_case(5.0, 0.0, "Multiplication by Zero: 5.0 * 0.0");
        
        // Test Case 5: Negative numbers
        send_test_case(-4.0, 2.0, "Negative Multiplication: -4.0 * 2.0");
        
        // Wait for pipeline to produce all results (3-stage pipeline)
        $display("Waiting for 3-stage pipeline results...\n");
        repeat(10) @(posedge clk);
        
        // Wait for remaining results
        begin
            int wait_cycles;
            wait_cycles = 0;
            while (expected_real_queue.size() > 0 && wait_cycles < 20) begin
                @(posedge clk);
                wait_cycles = wait_cycles + 1;
            end
        end
        
        // Final report
        $display("=== Test Summary ===");
        $display("Total Tests: %0d", test_count);
        $display("Passed: %0d", pass_count);
        $display("Failed: %0d", fail_count);
        $display("Remaining in queue: %0d", expected_real_queue.size());
        $display("Pipeline Depth: 3 stages");
        
        if (fail_count == 0 && expected_real_queue.size() == 0) begin
            $display("ALL TESTS PASSED!");
        end else begin
            $display("SOME TESTS FAILED OR INCOMPLETE!");
        end
        
        $finish;
    end
    
    // Timeout watchdog
    initial begin
        #20000; // 20us timeout
        $display("ERROR: Testbench timeout!");
        $display("Tests completed: %0d/%0d", pass_count + fail_count, test_count);
        $finish;
    end

endmodule
