// ======================
// Floating Point Subtractor Testbench
// ======================
`timescale 1ns / 1ps

module Floating_subtractor_tb;
    // Inputs
    reg [31:0] a;
    reg [31:0] b;
    reg enable;

    // Outputs
    wire [31:0] ans;

    // Instantiate the Unit Under Test (UUT)
    Top uut (
        .a(a),
        .b(b),
        .enable(enable),
        .ans(ans)
    );

    // Testbench logic
    initial begin
        // Initialize Inputs
        a = 0;
        b = 0;
        enable = 0;

        // Wait for global reset
        #100;

        // Enable the module
        enable = 1;

        // Test Case 1: Simple Subtraction (Positive Numbers)
        a = 32'h40000000; // 2.0 in IEEE 754
        b = 32'h3F800000; // 1.0 in IEEE 754
        #10;
        $display("Test Case 1: 2.0 - 1.0 = %h (Expected: 3F800000)", ans);

        // Test Case 2: Subtraction with Negative Result
        a = 32'h3F800000; // 1.0 in IEEE 754
        b = 32'h40000000; // 2.0 in IEEE 754
        #10;
        $display("Test Case 2: 1.0 - 2.0 = %h (Expected: BF800000)", ans);

        // Additional test cases...
        // [Rest of the subtraction test cases...]

        // End simulation
        $stop;
    end
endmodule


// ======================
// Floating Point Multiplication Testbench
// ======================
module tb_FloatingMultiplication;
    // Testbench signals
    logic [31:0] A;
    logic [31:0] B;
    logic clk;
    logic [31:0] result;

    // Instantiate the FloatingMultiplication module
    FloatingMultiplication uut (
        .A(A),
        .B(B),
        .clk(clk),
        .result(result)
    );

    // Clock generation
    initial begin
        clk = 0;
        forever #5 clk = ~clk;  // Generate clock with 10ns period (100MHz)
    end

    // Test cases
    initial begin
        // Test case 1: 1.0 * 2.0 = 2.0
        A = 32'h3F800000;  // 1.0
        B = 32'h40000000;  // 2.0
        #10;
        $display("Test 1: A = 1.0, B = 2.0, Result: %h", result);

        // Additional test cases...
        // [Rest of the multiplication test cases...]

        // Finish simulation
        $finish;
    end

    // Monitor output
    initial begin
        $monitor("Time = %t | A = %h, B = %h, Result = %h", $time, A, B, result);
    end
endmodule


// ======================
// Top Module Testbench (Division)
// ======================
module top_tb;
    logic         clock;
    logic         reset;
    logic [31:0]  a_signal, b_signal, result_signal;
    
    // Instantiate the top module
    TopModule dut (
        .a(a_signal),
        .b(b_signal),
        .reset(reset),
        .result(result_signal)
    );
    
    // Clock generation
    initial begin
        clock = 0;
        forever #5 clock = ~clock;
    end
    
    // Test procedure
    initial begin
        // Initialize signals
        reset = 1;
        a_signal = 0;
        b_signal = 0;
        
        // Reset the design
        #10 reset = 0;
        
        // Test case 1: 6.0 / 2.0 = 3.0
        // 6.0 = 0x40C00000, 2.0 = 0x40000000
        #10 a_signal = 32'h40C00000; b_signal = 32'h40000000;
        #20 $display("Test 1: %f / %f = %f (Expected 3.0)", 
                    $bitstoshortreal(a_signal), 
                    $bitstoshortreal(b_signal), 
                    $bitstoshortreal(result_signal));
        
        // Test case 2: 1.0 / 4.0 = 0.25
        // 1.0 = 0x3F800000, 4.0 = 0x40800000
        #10 a_signal = 32'h3F800000; b_signal = 32'h40800000;
        #20 $display("Test 2: %f / %f = %f (Expected 0.25)", 
                    $bitstoshortreal(a_signal), 
                    $bitstoshortreal(b_signal), 
                    $bitstoshortreal(result_signal));
        
        // Test case 3: -10.0 / 5.0 = -2.0
        // -10.0 = 0xC1200000, 5.0 = 0x40A00000
        #10 a_signal = 32'hC1200000; b_signal = 32'h40A00000;
        #20 $display("Test 3: %f / %f = %f (Expected -2.0)", 
                    $bitstoshortreal(a_signal), 
                    $bitstoshortreal(b_signal), 
                    $bitstoshortreal(result_signal));
        
        // Test case 4: 1.5 / 0.5 = 3.0
        // 1.5 = 0x3FC00000, 0.5 = 0x3F000000
        #10 a_signal = 32'h40800000; b_signal = 32'h3F19999A;
        #20 $display("Test 4: %f / %f = %f (Expected 3.0)", 
                    $bitstoshortreal(a_signal), 
                    $bitstoshortreal(b_signal), 
                    $bitstoshortreal(result_signal));
        
        // Test case 5: Division by zero (should return infinity)
        // 5.0 = 0x40A00000, 0.0 = 0x00000000
        #10 a_signal = 32'h40A00000; b_signal = 32'h00000000;
        #20 $display("Test 5: %f / %f = %f (Expected +inf)", 
                    $bitstoshortreal(a_signal), 
                    $bitstoshortreal(b_signal), 
                    $bitstoshortreal(result_signal));
        
        // Finish simulation
        #10 $finish;
    end
    
    // Monitor signals
    initial begin
        $monitor("At time %t: a=%h b=%h result=%h", 
                $time, a_signal, b_signal, result_signal);
    end
endmodule


// ======================
// Floating Point Addition Testbench
// ======================
module ieee754_adder_tb;

    // Inputs
    logic [31:0] A;
    logic [31:0] B;
    
    // Output
    logic [31:0] O;
    
    // Instantiate the Unit Under Test (UUT)
    ieee754_adder uut (
        .A(A),
        .B(B),
        .O(O)
    );
    
    // Function to display float value
    function string float_to_string(input logic [31:0] f);
        automatic logic sign = f[31];
        automatic logic [7:0] exponent = f[30:23];
        automatic logic [22:0] mantissa = f[22:0];
        return $sformatf("%s %8b %23b", sign ? "-" : "+", exponent, mantissa);
    endfunction
    
    initial begin
        // Test case 1: Positive + Positive (1.5 + 2.25 = 3.75)
        A = 32'h3FC00000; // 1.5
        B = 32'h40100000; // 2.25
        #10;
        $display("Test 1: 1.5 + 2.25 = 3.75");
        $display("A: %s", float_to_string(A));
        $display("B: %s", float_to_string(B));
        $display("O: %s", float_to_string(O));
        $display("Expected: 32'h40700000 (3.75)");
        $display("Actual:   %h", O);
        $display();
        
        // Test case 2: Negative + Positive (-1.5 + 2.25 = 0.75)
        A = 32'hBFC00000; // -1.5
        B = 32'h40100000; // 2.25
        #10;
        $display("Test 2: -1.5 + 2.25 = 0.75");
        $display("A: %s", float_to_string(A));
        $display("B: %s", float_to_string(B));
        $display("O: %s", float_to_string(O));
        $display("Expected: 32'h3F400000 (0.75)");
        $display("Actual:   %h", O);
        $display();
        
        // Test case 3: Positive + Negative (1.5 + -2.25 = -0.75)
        A = 32'h3FC00000; // 1.5
        B = 32'hC0100000; // -2.25
        #10;
        $display("Test 3: 1.5 + -2.25 = -0.75");
        $display("A: %s", float_to_string(A));
        $display("B: %s", float_to_string(B));
        $display("O: %s", float_to_string(O));
        $display("Expected: 32'hBF400000 (-0.75)");
        $display("Actual:   %h", O);
        $display();
        
        // Test case 4: Negative + Negative (-1.5 + -2.25 = -3.75)
        A = 32'hBFC00000; // -1.5
        B = 32'hC0100000; // -2.25
        #10;
        $display("Test 4: -1.5 + -2.25 = -3.75");
        $display("A: %s", float_to_string(A));
        $display("B: %s", float_to_string(B));
        $display("O: %s", float_to_string(O));
        $display("Expected: 32'hC0700000 (-3.75)");
        $display("Actual:   %h", O);
        $display();
        
        $finish;
    end

endmodule