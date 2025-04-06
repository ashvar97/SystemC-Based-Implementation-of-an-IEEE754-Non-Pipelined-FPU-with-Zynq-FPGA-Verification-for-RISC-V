// IEEE 754 Floating Point Multiplier Testbench

module tb_ieee754_multiplier;
    // Inputs
    logic [31:0] A;
    logic [31:0] B;
    logic clk;
    logic reset;
    
    // Output
    logic [31:0] result;

    // Instantiate Unit Under Test
    ieee754_multiplier uut (
        .A(A),
        .B(B),
        .reset(reset),
        .result(result)
    );

    // Clock generation (100MHz)
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    // Initialize reset
    initial begin
        reset = 1;
        #10 reset = 0;
    end

    // Test cases
    initial begin
        // Wait for reset to complete
        #15;
        
        // Test case 1: 1.0 * 2.0 = 2.0
        A = 32'h3F800000;  // 1.0 in IEEE 754
        B = 32'h40000000;  // 2.0 in IEEE 754
        #10;
        $display("Test 1: 1.0 * 2.0 = %h (Expected: 40000000)", result);
        
        // Test case 2: 0.5 * 4.0 = 2.0
        A = 32'h3F000000;  // 0.5
        B = 32'h40800000;  // 4.0
        #10;
        $display("Test 2: 0.5 * 4.0 = %h (Expected: 40000000)", result);
        
        // Test case 3: -1.5 * 3.0 = -4.5
        A = 32'hBFC00000;  // -1.5
        B = 32'h40400000;  // 3.0
        #10;
        $display("Test 3: -1.5 * 3.0 = %h (Expected: C0900000)", result);

        // Finish simulation
        $finish;
    end

    // Monitor changes
    initial begin
        $monitor("Time = %0t: A = %h (%0.2f), B = %h (%0.2f) → Result = %h (%0.2f)",
                $time, A, $bitstoshortreal(A), B, $bitstoshortreal(B), 
                result, $bitstoshortreal(result));
    end
endmodule


// IEEE 754 Floating Point Divider Testbench


module tb_ieee754_divider;
    // Inputs
    logic [31:0] a;
    logic [31:0] b;
    logic clk;
    logic reset;
    
    // Output
    logic [31:0] result;

    // Instantiate Unit Under Test
    ieee754_divider uut (
        .a(a),
        .b(b),
        .reset(reset),
        .result(result)
    );

    // Clock generation (100MHz)
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    // Test cases
    initial begin
        // Initialize and reset
        reset = 1;
        a = 0;
        b = 0;
        #10 reset = 0;
        
        // Test case 1: 6.0 / 2.0 = 3.0
        a = 32'h40C00000;  // 6.0
        b = 32'h40000000;  // 2.0
        #10;
        $display("Test 1: 6.0 / 2.0 = %h (Expected: 40400000)", result);
        
        // Test case 2: 1.0 / 4.0 = 0.25
        a = 32'h3F800000;  // 1.0
        b = 32'h40800000;  // 4.0
        #10;
        $display("Test 2: 1.0 / 4.0 = %h (Expected: 3E800000)", result);
        
        // Test case 3: -10.0 / 5.0 = -2.0
        a = 32'hC1200000;  // -10.0
        b = 32'h40A00000;  // 5.0
        #10;
        $display("Test 3: -10.0 / 5.0 = %h (Expected: C0000000)", result);

        // Test case 4: 1.5 / 0.5 = 3.0
        a = 32'h3FC00000;  // 1.5
        b = 32'h3F000000;  // 0.5
        #10;
        $display("Test 4: 1.5 / 0.5 = %h (Expected: 40400000)", result);

        // Test case 5: Division by zero
        a = 32'h40A00000;  // 5.0
        b = 32'h00000000;  // 0.0
        #10;
        $display("Test 5: 5.0 / 0.0 = %h (Expected: 7F800000)", result);

        // Finish simulation
        #10 $finish;
    end

    // Monitor changes
    initial begin
        $monitor("Time = %0t: a = %h (%0.2f), b = %h (%0.2f) → result = %h (%0.2f)",
                $time, a, $bitstoshortreal(a), b, $bitstoshortreal(b), 
                result, $bitstoshortreal(result));
    end
endmodule


// IEEE 754 Floating Point Adder Testbench


module tb_ieee754_adder;
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


// IEEE 754 Floating Point Subtractor Testbench


module tb_ieee754_subtractor;
    // Inputs
    reg [31:0] a;
    reg [31:0] b;
    reg enable;

    // Outputs
    wire [31:0] ans;

    // Instantiate the Unit Under Test (UUT)
    ieee754_subtractor uut (
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
        $display("Float Value: %f", $bitstoshortreal(ans));

        // Test Case 2: Subtraction with Negative Result
        a = 32'h3F800000; // 1.0 in IEEE 754
        b = 32'h40000000; // 2.0 in IEEE 754
        #10;
        $display("Test Case 2: 1.0 - 2.0 = %h (Expected: BF800000)", ans);
        $display("Float Value: %f", $bitstoshortreal(ans));

        // Test Case 3: Equal numbers
        a = 32'h40A00000; // 5.0
        b = 32'h40A00000; // 5.0
        #10;
        $display("Test Case 3: 5.0 - 5.0 = %h (Expected: 00000000)", ans);
        $display("Float Value: %f", $bitstoshortreal(ans));

        // Test Case 4: Large numbers
        a = 32'h461C4000; // 10000.0
        b = 32'h447A0000; // 1000.0
        #10;
        $display("Test Case 4: 10000.0 - 1000.0 = %h (Expected: 4611C000)", ans);
        $display("Float Value: %f", $bitstoshortreal(ans));

        // Test Case 5: Small numbers
        a = 32'h3A83126F; // 0.001
        b = 32'h3A12D0E5; // 0.0005
        #10;
        $display("Test Case 5: 0.001 - 0.0005 = %h (Expected: 3A03126F)", ans);
        $display("Float Value: %f", $bitstoshortreal(ans));

        // End simulation
        $stop;
    end

    // Monitor changes
    initial begin
        $monitor("Time = %0t: a = %h (%f), b = %h (%f) → ans = %h (%f)",
                $time, a, $bitstoshortreal(a), b, $bitstoshortreal(b), 
                ans, $bitstoshortreal(ans));
    end
endmodule

