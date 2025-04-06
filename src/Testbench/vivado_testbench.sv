// ======================
// Addition Testbench
// ======================
`timescale 1ns/1ps

module tb_adder;
    // Inputs
    reg [31:0] A;
    reg [31:0] B;
    
    // Outputs
    wire [31:0] result;
    
    // Instantiate DUT
    ieee754_adder dut (
        .A(A),
        .B(B),
        .O(result)
    );

    initial begin
        // Initialize VCD dump
        $dumpfile("waves_add.vcd");
        $dumpvars(0, tb_adder);
        
        // Test case 1: 3.14159265 + 2.0 = 5.14159265
        A = 32'h40490FDB; // 3.14159265
        B = 32'h40000000; // 2.0
        #50;
        $display("Test 1: %f + %f = %f",
                $bitstoshortreal(A),
                $bitstoshortreal(B),
                $bitstoshortreal(result));
        
        // Test case 2: 1.5 + (-0.5) = 1.0
        A = 32'h3FC00000; // 1.5
        B = 32'hBF000000; // -0.5
        #50;
        $display("Test 2: %f + %f = %f",
                $bitstoshortreal(A),
                $bitstoshortreal(B),
                $bitstoshortreal(result));
        
        $finish;
    end
endmodule


// ======================
// Subtraction Testbench
// ======================
`timescale 1ns/1ps

module tb_subtractor;
    // Inputs
    reg [31:0] a;
    reg [31:0] b;
    reg enable;
    
    // Outputs
    wire [31:0] ans;
    
    // Instantiate DUT
    ieee754_subtractor dut (
        .a(a),
        .b(b),
        .enable(enable),
        .ans(ans)
    );

    initial begin
        // Initialize VCD dump
        $dumpfile("waves_sub.vcd");
        $dumpvars(0, tb_subtractor);
        
        // Initialize
        enable = 0;
        a = 0;
        b = 0;
        #10 enable = 1;
        
        // Test case 1: 5.0 - 2.0 = 3.0
        a = 32'h40A00000; // 5.0
        b = 32'h40000000; // 2.0
        #50;
        $display("Test 1: %f - %f = %f",
                $bitstoshortreal(a),
                $bitstoshortreal(b),
                $bitstoshortreal(ans));
        
        // Test case 2: 1.5 - 0.5 = 1.0
        a = 32'h3FC00000; // 1.5
        b = 32'h3F000000; // 0.5
        #50;
        $display("Test 2: %f - %f = %f",
                $bitstoshortreal(a),
                $bitstoshortreal(b),
                $bitstoshortreal(ans));
        
        $finish;
    end
endmodule


// ======================
// Division Testbench
// ======================
`timescale 1ns/1ps

module tb_divider;
    // Inputs
    reg [31:0] a;
    reg [31:0] b;
    reg reset;
    
    // Outputs
    wire [31:0] result;
    
    // Instantiate DUT
    ieee754_div dut (
        .a(a),
        .b(b),
        .reset(reset),
        .result(result)
    );

    initial begin
        // Initialize VCD dump
        $dumpfile("waves_div.vcd");
        $dumpvars(0, tb_divider);
        
        // Test sequence
        reset = 1;
        a = 0;
        b = 0;
        #20 reset = 0;
        
        // Test case 1: 6.0 / 2.0 = 3.0
        a = 32'h40C00000; // 6.0
        b = 32'h40000000; // 2.0
        #100;
        $display("Result: %f / %f = %f",
                $bitstoshortreal(a),
                $bitstoshortreal(b),
                $bitstoshortreal(result));
        
        $finish;
    end
endmodule


// ======================
// Multiplication Testbench
// ======================
`timescale 1ns/1ps

module tb_multiplier;
    reg [31:0] A, B;
    reg reset;
    wire [31:0] result;
    
    ieee754mult dut(
        .A(A),
        .B(B),
        .reset(reset),
        .result(result)
    );
    
    initial begin
        $dumpfile("waves_mult.vcd");
        $dumpvars(0, tb_multiplier);
        
        // Initialize
        reset = 1;
        A = 0;
        B = 0;
        #20 reset = 0;
        
        // Test case 1: 2.5 * 4.0 = 10.0
        A = 32'h40200000; // 2.5
        B = 32'h40800000; // 4.0
        #50;
        $display("Test 1: %f * %f = %f", 
                $bitstoshortreal(A),
                $bitstoshortreal(B),
                $bitstoshortreal(result));
        
        // Test case 2: 1.5 * (-3.0) = -4.5
        A = 32'h3FC00000; // 1.5
        B = 32'hC0400000; // -3.0
        #50;
        $display("Test 2: %f * %f = %f", 
                $bitstoshortreal(A),
                $bitstoshortreal(B),
                $bitstoshortreal(result));
        
        $finish;
    end
endmodule