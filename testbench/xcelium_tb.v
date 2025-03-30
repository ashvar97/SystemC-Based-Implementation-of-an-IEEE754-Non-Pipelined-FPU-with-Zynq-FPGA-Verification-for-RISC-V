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
module top;
    logic         clock;
    logic         reset;
    logic [31:0]  a_signal, b_signal, a_significand_signal, b_significand_signal, result_signal;
    logic         a_sign_signal, b_sign_signal, normalized_signal;
    logic [7:0]   a_exp_signal, b_exp_signal;

    // Instantiate modules
    ExtractModule extract_module (
        .a(a_signal),
        .b(b_signal),
        .reset(reset),
        .a_significand(a_significand_signal),
        .b_significand(b_significand_signal),
        .a_sign(a_sign_signal),
        .b_sign(b_sign_signal),
        .a_exp(a_exp_signal),
        .b_exp(b_exp_signal)
    );

    // [Rest of the division testbench...]
    // [Module instantiations and test cases...]
endmodule


// ======================
// Floating Point Addition Testbench
// ======================
module FloatingAddition_tb;
    // Inputs
    logic [31:0] A;
    logic [31:0] B;
    logic clk;

    // Output
    logic [31:0] result;

    // Instantiate the Unit Under Test (UUT)
    FloatingAddition uut (
        .A(A),
        .B(B),
        .clk(clk),
        .result(result)
    );

    // Clock generation
    initial begin
        clk = 0;
        forever #5 clk = ~clk; // Toggle clock every 5 time units
    end

    // Test cases
    initial begin
        // Test case 1: Add two positive numbers
        A = 32'h3F800000; // 1.0 in IEEE 754
        B = 32'h40000000; // 2.0 in IEEE 754
        #10;
        $display("Test Case 1: 1.0 + 2.0 = %h (Expected: 40400000)", result);

        // Additional test cases...
        // [Rest of the addition test cases...]

        // End simulation
        $stop;
    end
endmodule
