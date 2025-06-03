//==============================================================================
//
// Fixed FP Pipelined Processor Design with IEEE 754 Operations
// Addresses synthesis and utilization reporting issues
//
//==============================================================================

//==============================================================================
//
// Module: FPPipelinedProcessor (Top Level)
//
module FPPipelinedProcessor
(
    input logic clk,
    input logic reset,
    input logic stall,
    output logic monitor_valid,
    output logic [7:0] monitor_pc
);

// Variables generated for SystemC signals
logic internal_stall;
logic [31:0] pc_out;
logic [31:0] ifu_instruction_out;
logic ifu_valid_out;
logic [31:0] op1_out;
logic [31:0] op2_out;
logic [4:0] rd_out;
logic reg_write_out;
logic decode_valid_out;
logic [31:0] decode_instruction_out;
logic [6:0] opcode;
logic [31:0] ex_result_out;
logic [4:0] ex_rd_out;
logic ex_reg_write_out;
logic ex_valid_out;
logic [31:0] ex_instruction_out;
logic [31:0] mem_result_out;
logic [4:0] mem_rd_out;
logic mem_reg_write_out;
logic mem_valid_out;
logic [31:0] mem_instruction_out;
logic [31:0] wb_result_out;
logic [4:0] wb_rd_out;
logic wb_reg_write_en;
logic wb_valid_out;
logic [31:0] reg_file[32];
logic [31:0] imem_address;
logic [31:0] imem_instruction;

// Initialize register file - Fixed to avoid infinite loops
initial begin
    for (int i = 0; i < 32; i++) begin
        reg_file[i] = 32'h00000000;
    end
    // Initialize some test values
    reg_file[1] = 32'h3F800000;  // 1.0 in IEEE754
    reg_file[2] = 32'h40000000;  // 2.0 in IEEE754
    reg_file[3] = 32'h40400000;  // 3.0 in IEEE754
    reg_file[4] = 32'h40800000;  // 4.0 in IEEE754
end

//------------------------------------------------------------------------------
// Method process: update_stall

always_comb 
begin : update_stall
    internal_stall = stall;
end

//------------------------------------------------------------------------------
// Method process: update_opcode

always_comb 
begin : update_opcode
    opcode = decode_instruction_out[31:25];
end

//------------------------------------------------------------------------------
// Method process: update_monitor

always_comb 
begin : update_monitor
    monitor_valid = wb_valid_out;
    monitor_pc = pc_out[7:0];
end

//------------------------------------------------------------------------------
// Clocked THREAD: ifu_process (Instruction Fetch Unit)

// Thread-local variables
logic terminated;
logic [31:0] pc;

// Synchronous register update - Fixed to remove redundant logic
always_ff @(posedge clk) 
begin : ifu_process_ff
    if (reset) begin
        pc <= 32'h0;
        terminated <= 1'b0;
        ifu_instruction_out <= 32'h0;
        ifu_valid_out <= 1'b0;
        pc_out <= 32'h0;
        imem_address <= 32'h0;
    end else if (!internal_stall && !terminated) begin
        imem_address <= pc;
        ifu_instruction_out <= imem_instruction;
        ifu_valid_out <= (imem_instruction != 32'h0);
        pc_out <= pc;
        
        if (imem_instruction == 32'h0) begin
            terminated <= 1'b1;
            ifu_valid_out <= 1'b0;
        end else begin
            pc <= pc + 32'd4;
        end
    end
end

//------------------------------------------------------------------------------
// Clocked THREAD: decode_process

always_ff @(posedge clk) 
begin : decode_process_ff
    if (reset) begin
        op1_out <= 32'h0;
        op2_out <= 32'h0;
        rd_out <= 5'h0;
        reg_write_out <= 1'b0;
        decode_valid_out <= 1'b0;
        decode_instruction_out <= 32'h0;
    end else if (!internal_stall) begin
        decode_valid_out <= ifu_valid_out;
        decode_instruction_out <= ifu_instruction_out;
        
        if (ifu_valid_out && ifu_instruction_out != 32'h0) begin
            logic [4:0] rs1, rs2, rd;
            rs1 = ifu_instruction_out[19:15];  // Fixed bit ranges for RISC-V
            rs2 = ifu_instruction_out[24:20];
            rd = ifu_instruction_out[11:7];
            
            op1_out <= (rs1 == 5'h0) ? 32'h0 : reg_file[rs1];  // x0 is always 0
            op2_out <= (rs2 == 5'h0) ? 32'h0 : reg_file[rs2];
            rd_out <= rd;
            reg_write_out <= (rd != 5'h0);  // Don't write to x0
        end else begin
            op1_out <= 32'h0;
            op2_out <= 32'h0;
            rd_out <= 5'h0;
            reg_write_out <= 1'b0;
        end
    end
end

//------------------------------------------------------------------------------
// Clocked THREAD: reg_file_update

always_ff @(posedge clk) 
begin : reg_file_update_ff
    if (reset) begin
        // Reset handled in initial block
    end else if (wb_reg_write_en && wb_valid_out && wb_rd_out != 5'h0) begin
        reg_file[wb_rd_out] <= wb_result_out;
    end
end

//------------------------------------------------------------------------------
// Child module instances

InstructionMemory imem
(
    .address(imem_address),
    .instruction(imem_instruction)
);

Execute execute
(
    .clk(clk),
    .reset(reset),
    .stall(internal_stall),
    .valid_in(decode_valid_out),
    .op1(op1_out),
    .op2(op2_out),
    .opcode(opcode),
    .rd_in(rd_out),
    .reg_write_in(reg_write_out),
    .instruction_in(decode_instruction_out),
    .result_out(ex_result_out),
    .rd_out(ex_rd_out),
    .reg_write_out(ex_reg_write_out),
    .valid_out(ex_valid_out),
    .instruction_out(ex_instruction_out)
);

Memory memory
(
    .clk(clk),
    .reset(reset),
    .stall(internal_stall),
    .valid_in(ex_valid_out),
    .result_in(ex_result_out),
    .rd_in(ex_rd_out),
    .reg_write_in(ex_reg_write_out),
    .instruction_in(ex_instruction_out),
    .result_out(mem_result_out),
    .rd_out(mem_rd_out),
    .reg_write_out(mem_reg_write_out),
    .valid_out(mem_valid_out),
    .instruction_out(mem_instruction_out)
);

Writeback writeback
(
    .clk(clk),
    .reset(reset),
    .stall(internal_stall),
    .valid_in(mem_valid_out),
    .result_in(mem_result_out),
    .rd_in(mem_rd_out),
    .reg_write_in(mem_reg_write_out),
    .instruction_in(mem_instruction_out),
    .result_out(wb_result_out),
    .rd_out(wb_rd_out),
    .reg_write_en(wb_reg_write_en),
    .valid_out(wb_valid_out)
);

endmodule

//==============================================================================
//
// Module: InstructionMemory
//
module InstructionMemory
(
    input logic [31:0] address,
    output logic [31:0] instruction
);

logic [31:0] imem[256];

// Initialize instruction memory with a test program
initial begin
    // Initialize all memory to 0
    for (int i = 0; i < 256; i++) begin
        imem[i] = 32'h00000000;
    end
    
    // Test program: floating point operations using IEEE 754
    // Using custom encoding for FP operations
    imem[0] = 32'h00208AB3;   // FADD: r21 = r1 + r2
    imem[1] = 32'h40208B33;   // FSUB: r22 = r1 - r2  
    imem[2] = 32'h80118BB3;   // FMUL: r23 = r1 * r3
    imem[3] = 32'hC0220C33;   // FDIV: r24 = r4 / r2
    imem[4] = 32'h00000000;   // NOP - terminates program
end

//------------------------------------------------------------------------------
// Memory read process

always_comb 
begin : process_read
    if (address < 32'd1024) begin  // 256 * 4 bytes = 1024
        instruction = imem[address[9:2]];
    end else begin
        instruction = 32'h00000000;  // Return NOP for out-of-bounds
    end
end

endmodule

//==============================================================================
//
// Module: Execute (Enhanced with IEEE 754 Operations)
//
module Execute (
    input logic clk,
    input logic reset,
    input logic stall,
    input logic valid_in,
    input logic [31:0] op1,
    input logic [31:0] op2,
    input logic [6:0] opcode,
    input logic [4:0] rd_in,
    input logic reg_write_in,
    input logic [31:0] instruction_in,
    output logic [31:0] result_out,
    output logic [4:0] rd_out,
    output logic reg_write_out,
    output logic valid_out,
    output logic [31:0] instruction_out
);

// IEEE 754 operation results
logic [31:0] fadd_result;
logic [31:0] fsub_result;
logic [31:0] fmul_result;
logic [31:0] fdiv_result;
logic [31:0] fp_result;

// Pipeline registers
always_ff @(posedge clk) begin
    if (reset) begin
        result_out <= 32'h0;
        rd_out <= 5'h0;
        reg_write_out <= 1'b0;
        valid_out <= 1'b0;
        instruction_out <= 32'h0;
    end else if (!stall) begin
        valid_out <= valid_in;
        rd_out <= rd_in;
        reg_write_out <= reg_write_in;
        instruction_out <= instruction_in;
        result_out <= fp_result;
    end
end

// Operation selection
always_comb begin
    case (opcode[6:0])
        7'b0000000: fp_result = fadd_result;  // FADD
        7'b0100000: fp_result = fsub_result;  // FSUB
        7'b1000000: fp_result = fmul_result;  // FMUL
        7'b1100000: fp_result = fdiv_result;  // FDIV
        default:    fp_result = 32'h0;
    endcase
end

//------------------------------------------------------------------------------
// IEEE 754 Floating Point Units

// IEEE 754 Adder
ieee754_adder fp_adder (
    .A(op1),
    .B(op2),
    .O(fadd_result)
);

// IEEE 754 Subtractor
ieee754_subtractor fp_subtractor (
    .a(op1),
    .b(op2),
    .enable(valid_in && (opcode[6:0] == 7'b0100000)),
    .ans(fsub_result)
);

// IEEE 754 Multiplier
ieee754mult fp_multiplier (
    .A(op1),
    .B(op2),
    .reset(reset),
    .result(fmul_result)
);

// IEEE 754 Divider
ieee754_div fp_divider (
    .a(op1),
    .b(op2),
    .reset(reset),
    .result(fdiv_result)
);

endmodule

//==============================================================================
//
// Module: Memory (Pipeline Stage)
//
module Memory (
    input logic clk,
    input logic reset,
    input logic stall,
    input logic valid_in,
    input logic [31:0] result_in,
    input logic [4:0] rd_in,
    input logic reg_write_in,
    input logic [31:0] instruction_in,
    output logic [31:0] result_out,
    output logic [4:0] rd_out,
    output logic reg_write_out,
    output logic valid_out,
    output logic [31:0] instruction_out
);

// Pipeline registers
always_ff @(posedge clk) begin
    if (reset) begin
        result_out <= 32'h0;
        rd_out <= 5'h0;
        reg_write_out <= 1'b0;
        valid_out <= 1'b0;
        instruction_out <= 32'h0;
    end else if (!stall) begin
        result_out <= result_in;
        rd_out <= rd_in;
        reg_write_out <= reg_write_in;
        valid_out <= valid_in;
        instruction_out <= instruction_in;
    end
end

endmodule

//==============================================================================
//
// Module: Writeback
//
module Writeback
(
    input logic clk,
    input logic reset,
    input logic stall,
    input logic valid_in,
    input logic [31:0] result_in,
    input logic [4:0] rd_in,
    input logic reg_write_in,
    input logic [31:0] instruction_in,
    output logic [31:0] result_out,
    output logic [4:0] rd_out,
    output logic reg_write_en,
    output logic valid_out
);

// Pipeline registers
always_ff @(posedge clk) begin
    if (reset) begin
        result_out <= 32'h0;
        rd_out <= 5'h0;
        reg_write_en <= 1'b0;
        valid_out <= 1'b0;
    end else if (!stall) begin
        result_out <= result_in;
        rd_out <= rd_in;
        reg_write_en <= reg_write_in && valid_in && (instruction_in != 32'h0);
        valid_out <= valid_in;
    end
end

endmodule

//==============================================================================
//
// IEEE 754 FLOATING POINT MODULES
//
//==============================================================================

//==============================================================================
//
// Module: ieee754_adder
//
module ieee754_adder
(
    input logic [31:0] A,
    input logic [31:0] B,
    output logic [31:0] O
);

logic sign_a, sign_b, out_sign;
logic [7:0] exp_a, exp_b, out_exponent;
logic [23:0] mant_a, mant_b;
logic [24:0] out_mantissa;

ieee754_extractor extractA (
    .A(A),
    .sign(sign_a),
    .exponent(exp_a),
    .mantissa(mant_a)
);

ieee754_extractor extractB (
    .A(B),
    .sign(sign_b),
    .exponent(exp_b),
    .mantissa(mant_b)
);

ieee754_adder_core adderCore (
    .exp_a(exp_a),
    .exp_b(exp_b),
    .mant_a(mant_a),
    .mant_b(mant_b),
    .sign_a(sign_a),
    .sign_b(sign_b),
    .out_sign(out_sign),
    .out_exponent(out_exponent),
    .out_mantissa(out_mantissa)
);

ieee754_normalizer normalizer (
    .exponent(out_exponent),
    .mantissa(out_mantissa),
    .sign(out_sign),
    .result(O)
);

endmodule

//==============================================================================
//
// Module: ieee754_extractor
//
module ieee754_extractor
(
    input logic [31:0] A,
    output logic sign,
    output logic [7:0] exponent,
    output logic [23:0] mantissa
);

always_comb begin
    sign = A[31];
    exponent = A[30:23];
    if (exponent == 0) begin
        mantissa = {1'b0, A[22:0]};
    end else begin
        mantissa = {1'b1, A[22:0]};
    end
end

endmodule

//==============================================================================
//
// Module: ieee754_adder_core
//
module ieee754_adder_core
(
    input logic [7:0] exp_a,
    input logic [7:0] exp_b,
    input logic [23:0] mant_a,
    input logic [23:0] mant_b,
    input logic sign_a,
    input logic sign_b,
    output logic out_sign,
    output logic [7:0] out_exponent,
    output logic [24:0] out_mantissa
);

always_comb begin
    logic [7:0] diff;
    logic [23:0] tmp_mantissa;
    logic a_is_nan, b_is_nan, a_is_inf, b_is_inf;
    
    diff = 0;
    tmp_mantissa = 0;
    a_is_nan = (exp_a == 8'hFF) && (mant_a[22:0] != 0);
    b_is_nan = (exp_b == 8'hFF) && (mant_b[22:0] != 0);
    a_is_inf = (exp_a == 8'hFF) && (mant_a[22:0] == 0);
    b_is_inf = (exp_b == 8'hFF) && (mant_b[22:0] == 0);
    
    if (a_is_nan || b_is_nan) begin
        out_exponent = 8'hFF;
        out_mantissa = 25'h1000000;
        out_sign = 0;
    end else if (a_is_inf || b_is_inf) begin
        if (a_is_inf && b_is_inf) begin
            if (sign_a == sign_b) begin
                out_exponent = 8'hFF;
                out_mantissa = 0;
                out_sign = sign_a;
            end else begin
                out_exponent = 8'hFF;
                out_mantissa = 25'h1000000;
                out_sign = 0;
            end
        end else begin
            out_exponent = 8'hFF;
            out_mantissa = 0;
            out_sign = a_is_inf ? sign_a : sign_b;
        end
    end else if (exp_a == 0 && mant_a == 0) begin
        out_sign = sign_b;
        out_exponent = exp_b;
        out_mantissa = {1'b0, mant_b};
    end else if (exp_b == 0 && mant_b == 0) begin
        out_sign = sign_a;
        out_exponent = exp_a;
        out_mantissa = {1'b0, mant_a};
    end else begin
        if (exp_a > exp_b) begin
            diff = exp_a - exp_b;
            tmp_mantissa = mant_b >> diff;
            out_exponent = exp_a;
            if (sign_a == sign_b) begin
                out_mantissa = {1'b0, mant_a} + {1'b0, tmp_mantissa};
            end else begin
                if (mant_a >= tmp_mantissa) begin
                    out_mantissa = {1'b0, mant_a} - {1'b0, tmp_mantissa};
                    out_sign = sign_a;
                end else begin
                    out_mantissa = {1'b0, tmp_mantissa} - {1'b0, mant_a};
                    out_sign = sign_b;
                end
            end
        end else if (exp_b > exp_a) begin
            diff = exp_b - exp_a;
            tmp_mantissa = mant_a >> diff;
            out_exponent = exp_b;
            if (sign_a == sign_b) begin
                out_mantissa = {1'b0, mant_b} + {1'b0, tmp_mantissa};
            end else begin
                if (mant_b >= tmp_mantissa) begin
                    out_mantissa = {1'b0, mant_b} - {1'b0, tmp_mantissa};
                    out_sign = sign_b;
                end else begin
                    out_mantissa = {1'b0, tmp_mantissa} - {1'b0, mant_b};
                    out_sign = sign_a;
                end
            end
        end else begin
            out_exponent = exp_a;
            if (sign_a == sign_b) begin
                out_mantissa = {1'b0, mant_a} + {1'b0, mant_b};
            end else begin
                if (mant_a > mant_b) begin
                    out_mantissa = {1'b0, mant_a} - {1'b0, mant_b};
                    out_sign = sign_a;
                end else begin
                    out_mantissa = {1'b0, mant_b} - {1'b0, mant_a};
                    out_sign = sign_b;
                end
            end
        end
        
        if (out_mantissa == 0) begin
            out_sign = 0;
            out_exponent = 0;
        end
    end
end

endmodule

//==============================================================================
//
// Module: ieee754_normalizer
//
module ieee754_normalizer
(
    input logic [7:0] exponent,
    input logic [24:0] mantissa,
    input logic sign,
    output logic [31:0] result
);

always_comb begin
    logic [4:0] lz;
    logic [7:0] norm_exponent;
    logic [24:0] norm_mantissa;
    
    lz = 0;
    norm_exponent = exponent;
    norm_mantissa = mantissa;
    
    if (exponent == 8'hFF) begin
        result = {sign, exponent, mantissa[22:0]};
    end else if (mantissa == 0) begin
        result = 0;
    end else begin
        if (norm_mantissa[24]) begin
            norm_exponent = norm_exponent + 1;
            norm_mantissa = norm_mantissa >> 1;
        end else if (norm_mantissa[23] == 0 && norm_exponent != 0) begin
            // Count leading zeros
            for (lz = 0; lz < 24 && norm_mantissa[23-lz] == 0; lz++) begin
            end
            if (norm_exponent > lz) begin
                norm_exponent = norm_exponent - lz;
                norm_mantissa = norm_mantissa << lz;
            end else begin
                norm_mantissa = norm_mantissa << (norm_exponent - 1);
                norm_exponent = 0;
            end
        end
        
        if (norm_exponent >= 8'hFF) begin
            result = {sign, 8'hFF, 23'h0}; // Infinity
        end else begin
            result = {sign, norm_exponent, norm_mantissa[22:0]};
        end
    end
end

endmodule

//==============================================================================
//
// Module: ieee754_subtractor
//
module ieee754_subtractor
(
    input logic [31:0] a,
    input logic [31:0] b,
    input logic enable,
    output logic [31:0] ans
);

always_comb begin
    logic [31:0] val_b, val_s, result;
    logic [23:0] aligned;
    logic [24:0] sum, sum_norm;
    logic [4:0] lead0;
    logic sig_a, sig_b, result_sign;
    
    if (enable) begin
        sig_a = a[31];
        sig_b = !b[31]; // Invert sign for subtraction
        
        if (a[30:0] > b[30:0]) begin
            val_b = a;
            val_s = b;
            result_sign = sig_a;
        end else begin
            val_b = b;
            val_s = a;
            result_sign = sig_b;
        end
        
        aligned = {1'b1, val_s[22:0]};
        aligned = aligned >> (val_b[30:23] - val_s[30:23]);
        
        if (sig_a == sig_b) begin
            sum = {1'b1, val_b[22:0]} + aligned;
        end else begin
            sum = {1'b1, val_b[22:0]} - aligned;
        end
        
        if (sum == 0) begin
            result = 0;
        end else begin
            // Find leading zeros
            lead0 = 0;
            for (int i = 23; i >= 0; i--) begin
                if (sum[i]) begin
                    lead0 = 23 - i;
                    break;
                end
            end
            
            sum_norm = sum << lead0;
            
            if (sum[24]) begin
                result[30:23] = val_b[30:23] + 1;
                result[22:0] = sum[23:1];
            end else begin
                if (lead0 > val_b[30:23]) begin
                    result = 0;
                end else begin
                    result[30:23] = val_b[30:23] - lead0;
                    result[22:0] = sum_norm[22:0];
                end
            end
            result[31] = result_sign;
        end
        ans = result;
    end else begin
        ans = 0;
    end
end

endmodule

//==============================================================================
//
// Module: ieee754mult
//
module ieee754mult
(
    input logic [31:0] A,
    input logic [31:0] B,
    input logic reset,
    output logic [31:0] result
);

logic A_sign, B_sign, Sign;
logic [7:0] A_Exponent, B_Exponent, Temp_Exponent;
logic [23:0] A_Mantissa, B_Mantissa;
logic [47:0] Temp_Mantissa;

FloatingPointExtractor extractA (
    .in(A),
    .reset(reset),
    .sign(A_sign),
    .exponent(A_Exponent),
    .mantissa(A_Mantissa)
);

FloatingPointExtractor extractB (
    .in(B),
    .reset(reset),
    .sign(B_sign),
    .exponent(B_Exponent),
    .mantissa(B_Mantissa)
);

FloatingPointMultiplier multiply (
    .A_Mantissa(A_Mantissa),
    .B_Mantissa(B_Mantissa),
    .A_Exponent(A_Exponent),
    .B_Exponent(B_Exponent),
    .A_sign(A_sign),
    .B_sign(B_sign),
    .reset(reset),
    .Temp_Mantissa(Temp_Mantissa),
    .Temp_Exponent(Temp_Exponent),
    .Sign(Sign)
);

FloatingPointNormalizer normalize (
    .Temp_Mantissa(Temp_Mantissa),
    .Temp_Exponent(Temp_Exponent),
    .Sign(Sign),
    .reset(reset),
    .result(result)
);

endmodule

//==============================================================================
//
// Module: FloatingPointExtractor
//
module FloatingPointExtractor
(
    input logic [31:0] in,
    input logic reset,
    output logic sign,
    output logic [7:0] exponent,
    output logic [23:0] mantissa
);

always_comb begin
    if (reset) begin
        sign = 0;
        exponent = 0;
        mantissa = 0;
    end else begin
        sign = in[31];
        exponent = in[30:23];
        mantissa = {1'b1, in[22:0]};
    end
end

endmodule

//==============================================================================
//
// Module: FloatingPointMultiplier
//
module FloatingPointMultiplier
(
    input logic [23:0] A_Mantissa,
    input logic [23:0] B_Mantissa,
    input logic [7:0] A_Exponent,
    input logic [7:0] B_Exponent,
    input logic A_sign,
    input logic B_sign,
    input logic reset,
    output logic [47:0] Temp_Mantissa,
    output logic [7:0] Temp_Exponent,
    output logic Sign
);

always_comb begin
    if (reset) begin
        Temp_Mantissa = 0;
        Temp_Exponent = 0;
        Sign = 0;
    end else begin
        Temp_Mantissa = A_Mantissa * B_Mantissa;
        Temp_Exponent = A_Exponent + B_Exponent - 127;
        Sign = A_sign ^ B_sign;
    end
end

endmodule

//==============================================================================
//
// Module: FloatingPointNormalizer (IEEE754Mult.h:116:5)
//
module FloatingPointNormalizer // "system.execute.fp_multiplier.normalize"
(
    input logic [47:0] Temp_Mantissa,
    input logic [7:0] Temp_Exponent,
    input logic Sign,
    input logic reset,
    output logic [31:0] result
);

//------------------------------------------------------------------------------
// Method process: normalize (IEEE754Mult.h:73:5) 

always_comb 
begin : normalize     // IEEE754Mult.h:73:5
    logic [22:0] Mantissa;
    logic [7:0] Exponent;
    Mantissa = 0;
    Exponent = 0;
    if (reset)
    begin
        result = 0;
    end else begin
        Mantissa = 0;
        Exponent = 0;
        if (Temp_Mantissa[47])
        begin
            Mantissa = Temp_Mantissa[46 : 24];
            Exponent = Temp_Exponent + 1;
        end else begin
            Mantissa = Temp_Mantissa[45 : 23];
            Exponent = Temp_Exponent;
        end
        result = {Sign, Exponent, Mantissa};
    end
end

endmodule



//==============================================================================
//
// Module: ieee754_div ()
//
module ieee754_div // "system.execute.fp_divider"
(
    input logic [31:0] a,
    input logic [31:0] b,
    input logic reset,
    output logic [31:0] result
);

// Variables generated for SystemC signals
logic [31:0] a_significand;
logic [31:0] b_significand;
logic a_sign;
logic b_sign;
logic [7:0] a_exp;
logic [7:0] b_exp;


//------------------------------------------------------------------------------
// Child module instances

ExtractModule extract_module
(
  .a(a),
  .b(b),
  .reset(reset),
  .a_significand(a_significand),
  .b_significand(b_significand),
  .a_sign(a_sign),
  .b_sign(b_sign),
  .a_exp(a_exp),
  .b_exp(b_exp)
);

ComputeModule compute_module
(
  .a_significand(a_significand),
  .b_significand(b_significand),
  .a_sign(a_sign),
  .b_sign(b_sign),
  .a_exp(a_exp),
  .b_exp(b_exp),
  .reset(reset),
  .result(result)
);

NormalizationModule normalization_module
(
  .result(result),
  .a_exp(a_exp),
  .reset(reset)
);

endmodule



//==============================================================================
//
// Module: ExtractModule (IEEE754Div.h:154:5)
//
module ExtractModule // "system.execute.fp_divider.extract_module"
(
    input logic [31:0] a,
    input logic [31:0] b,
    input logic reset,
    output logic [31:0] a_significand,
    output logic [31:0] b_significand,
    output logic a_sign,
    output logic b_sign,
    output logic [7:0] a_exp,
    output logic [7:0] b_exp
);

//------------------------------------------------------------------------------
// Method process: extract (IEEE754Div.h:15:5) 

always_comb 
begin : extract     // IEEE754Div.h:15:5
    if (reset)
    begin
        a_significand = 0;
        b_significand = 0;
        a_sign = 0;
        b_sign = 0;
        a_exp = 0;
        b_exp = 0;
    end else begin
        a_exp = (a & 'h7F800000) >>> 23;
        b_exp = (b & 'h7F800000) >>> 23;
        a_sign = (a & 32'h80000000) != 0;
        b_sign = (b & 32'h80000000) != 0;
        a_significand = (a & 'h7FFFFF) | 'h800000;
        b_significand = (b & 'h7FFFFF) | 'h800000;
    end
end

endmodule



//==============================================================================
//
// Module: ComputeModule (IEEE754Div.h:155:5)
//
module ComputeModule // "system.execute.fp_divider.compute_module"
(
    input logic [31:0] a_significand,
    input logic [31:0] b_significand,
    input logic a_sign,
    input logic b_sign,
    input logic [7:0] a_exp,
    input logic [7:0] b_exp,
    input logic reset,
    output logic [31:0] result
);

//------------------------------------------------------------------------------
// Method process: compute (IEEE754Div.h:50:5) 

always_comb 
begin : compute     // IEEE754Div.h:50:5
    logic [31:0] r;
    logic [7:0] result_exp;
    logic [4:0] i;
    logic odd;
    logic rnd;
    logic sticky;
    logic [31:0] x_val;
    logic [31:0] y_val;
    logic [7:0] shift;
    logic result_sign;
    r = 0;
    result_exp = 0;
    i = 0;
    odd = 0;
    rnd = 0;
    sticky = 0;
    x_val = 0;
    y_val = 0;
    shift = 0;
    result_sign = 0;
    if (reset)
    begin
        result = 0;
    end else begin
        r = 0;
        result_exp = 0;
        i = 0;
        x_val = 0;
        y_val = 0;
        shift = 0;
        result_sign = a_sign ^ b_sign;
        result_exp = a_exp - b_exp + 127;
        x_val = a_significand;
        y_val = b_significand;
        if (x_val < y_val)
        begin
            x_val = x_val <<< 1;
            result_exp = result_exp - 1;
        end
        r = 0;
        for (i = 0; i < 25; i++)
        begin
            r = r <<< 1;
            if (x_val >= y_val)
            begin
                x_val = x_val - y_val;
                r = r | 1;
            end
            x_val = x_val <<< 1;
        end
        sticky = x_val != 0;
        if ((result_exp >= 1) && (result_exp <= 254))
        begin
            rnd = |((r & 'h1000000) >>> 24);
            odd = (r & 'h2) != 0;
            r = (r >>> 1) + (rnd & (sticky | odd));
            r = (result_exp <<< 23) + (r - 'h800000);
        end else begin
            if (result_exp > 254)
            begin
                r = 'h7F800000;
            end else begin
                shift = 1 - result_exp;
                if (shift > 25)
                begin
                    shift = 25;
                end
                sticky = sticky | ((r & ~(~0 <<< shift)) != 0);
                r = r >>> shift;
                rnd = |((r & 'h1000000) >>> 24);
                odd = (r & 'h2) != 0;
                r = (r >>> 1) + (rnd & (sticky | odd));
            end
        end
        r = r | (result_sign ? 32'h80000000 : 0);
        result = r;
    end
end

endmodule



//==============================================================================
//
// Module: NormalizationModule (IEEE754Div.h:156:5)
//
module NormalizationModule // "system.execute.fp_divider.normalization_module"
(
    input logic [31:0] result,
    input logic [7:0] a_exp,
    input logic reset
);

//------------------------------------------------------------------------------
// Method process: normalize (IEEE754Div.h:128:5) 
// Empty process, no code generated 

endmodule


