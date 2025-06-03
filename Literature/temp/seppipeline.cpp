module Testbench;
    // Clock and control signals
    logic clk = 0;
    logic reset = 1;
    logic stall = 0;
    always #5 clk = ~clk;
    
    // IFU outputs
    logic [31:0] pc_out;
    logic [31:0] ifu_instruction_out;
    logic ifu_valid_out;
    
    // Decode outputs
    logic [31:0] id_op1_out, id_op2_out;
    logic [4:0] id_rd_out;
    logic id_reg_write_out;
    logic id_valid_out;
    logic [31:0] id_instruction_out;
    
    // Execute outputs
    logic [31:0] ex_result_out;
    logic [4:0] ex_rd_out;
    logic ex_reg_write_out;
    logic ex_valid_out;
    logic [31:0] ex_instruction_out;
    
    // Memory outputs
    logic [31:0] mem_result_out;
    logic [4:0] mem_rd_out;
    logic mem_reg_write_out;
    logic mem_valid_out;
    logic [31:0] mem_instruction_out;
    
    // Writeback outputs
    logic [31:0] wb_result_out;
    logic [4:0] wb_rd_out;
    logic wb_reg_write_en;
    logic wb_valid_out;
    
    // Register file
    shortreal reg_file [0:31];

    // Instantiate IFU
    IFU ifu(
        .clk(clk),
        .reset(reset),
        .stall(stall),
        .pc_out(pc_out),
        .instruction_out(ifu_instruction_out),
        .valid_out(ifu_valid_out)
    );
    
    // Initialize instruction memory
    initial begin
        // RISC-V FPU instructions
        ifu.imem[0] = 32'b0000000_00010_00001_000_10000_1010011;  // fadd.s f16, f1, f2
        ifu.imem[1] = 32'b0000100_00101_00100_000_10001_1010011;  // fsub.s f17, f4, f5
        ifu.imem[2] = 32'b0001000_01000_00111_000_10010_1010011;  // fmul.s f18, f7, f8
        ifu.imem[3] = 32'b0001100_01011_01010_000_10011_1010011;  // fdiv.s f19, f10, f11
        ifu.imem[4] = 32'h00000000;   // Terminate
        for (int i=5; i<1024; i++) ifu.imem[i] = 0;
    end

    // Instantiate Decode
    Decode decode(
        .clk(clk),
        .reset(reset),
        .stall(stall),
        .valid_in(ifu_valid_out),
        .instruction_in(ifu_instruction_out),
        .reg_file(reg_file),
        .op1_out(id_op1_out),
        .op2_out(id_op2_out),
        .rd_out(id_rd_out),
        .reg_write_out(id_reg_write_out),
        .valid_out(id_valid_out),
        .instruction_out(id_instruction_out)
    );

    // Instantiate Execute
    Execute execute(
        .clk(clk),
        .reset(reset),
        .stall(stall),
        .valid_in(id_valid_out),
        .op1(id_op1_out),
        .op2(id_op2_out),
        .opcode(id_instruction_out[31:25]), // funct7 bits
        .rd_in(id_rd_out),
        .reg_write_in(id_reg_write_out),
        .instruction_in(id_instruction_out),
        .result_out(ex_result_out),
        .rd_out(ex_rd_out),
        .reg_write_out(ex_reg_write_out),
        .valid_out(ex_valid_out),
        .instruction_out(ex_instruction_out)
    );

    // Instantiate Memory
    Memory memory(
        .clk(clk),
        .reset(reset),
        .stall(stall),
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

    // Instantiate Writeback
    Writeback writeback(
        .clk(clk),
        .reset(reset),
        .stall(stall),
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

    initial begin
        // Initialize register file
        reg_file[1] = 5.5;    // f1
        reg_file[2] = 2.5;    // f2
        reg_file[4] = 10.0;   // f4
        reg_file[5] = 3.0;    // f5
        reg_file[7] = 4.0;    // f7
        reg_file[8] = 2.5;    // f8
        reg_file[10] = 15.0;  // f10
        reg_file[11] = 3.0;   // f11
        
        #10 reset = 0;
        #200;
        
        $display("\nFinal Results:");
        $display("f16 (5.5 + 2.5) = %f (expected 8.0)", reg_file[16]);
        $display("f17 (10.0 - 3.0) = %f (expected 7.0)", reg_file[17]);
        $display("f18 (4.0 × 2.5) = %f (expected 10.0)", reg_file[18]);
        $display("f19 (15.0 ÷ 3.0) = %f (expected 5.0)", reg_file[19]);
        
        $finish;
    end

    // Register file update
    always @(posedge clk) begin
        if (wb_reg_write_en && wb_valid_out) begin
            if (wb_rd_out inside {16,17,18,19}) begin
                reg_file[wb_rd_out] = $bitstoshortreal(wb_result_out);
                $display("[%0tns] WB: f%d = %f", $time, wb_rd_out, 
                        $bitstoshortreal(wb_result_out));
            end
        end
    end
endmodule




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
    always_ff @(posedge clk or posedge reset) begin
        if (reset) begin
            result_out <= 0;
            rd_out <= 0;
            reg_write_out <= 0;
            valid_out <= 0;
            instruction_out <= 0;
        end
        else if (!stall) begin
            result_out <= result_in;
            rd_out <= rd_in;
            reg_write_out <= reg_write_in;
            valid_out <= valid_in;
            instruction_out <= instruction_in;
        end
    end
endmodule

module Writeback (
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
    always_ff @(posedge clk or posedge reset) begin
        if (reset) begin
            result_out <= 0;
            rd_out <= 0;
            reg_write_en <= 0;
            valid_out <= 0;
        end
        else if (!stall) begin
            result_out <= result_in;
            rd_out <= rd_in;
            reg_write_en <= reg_write_in && valid_in && (instruction_in != 0);
            valid_out <= valid_in;
        end
    end
endmodule







module Execute (
    input logic clk,
    input logic reset,
    input logic stall,
    input logic valid_in,
    input logic [31:0] op1, op2,
    input logic [6:0] opcode,  // Full 7-bit funct7 field
    input logic [4:0] rd_in,
    input logic reg_write_in,
    input logic [31:0] instruction_in,
    
    output logic [31:0] result_out,
    output logic [4:0] rd_out,
    output logic reg_write_out,
    output logic valid_out,
    output logic [31:0] instruction_out
);
    shortreal a_real, b_real, r_real;
    
    always_ff @(posedge clk or posedge reset) begin
        if (reset) begin
            result_out <= 0;
            rd_out <= 0;
            reg_write_out <= 0;
            valid_out <= 0;
            instruction_out <= 0;
        end
        else if (!stall) begin
            valid_out <= valid_in;
            rd_out <= rd_in;
            reg_write_out <= reg_write_in;
            instruction_out <= instruction_in;
            
            if (valid_in && reg_write_in) begin
                a_real = $bitstoshortreal(op1);
                b_real = $bitstoshortreal(op2);

                case(opcode[6:0])
                    7'b0000000: r_real = a_real + b_real;  // FADD
                    7'b0000100: r_real = a_real - b_real;  // FSUB
                    7'b0001000: r_real = a_real * b_real;  // FMUL
                    7'b0001100: r_real = a_real / b_real;  // FDIV
                    default:    r_real = 0.0;
                endcase

                result_out <= $shortrealtobits(r_real);
                $display("[EX] Opcode: %7b A: %f B: %f Result: %f", 
                        opcode, a_real, b_real, r_real);
            end
        end
    end
endmodule






module IFU (
    input logic clk,
    input logic reset,
    input logic stall,
    output logic [31:0] pc_out,
    output logic [31:0] instruction_out,
    output logic valid_out
);
    logic [31:0] pc;
    logic [31:0] imem [0:1023];
    logic terminated;
    
    // Initialize instruction memory
    initial begin
        // RISC-V FPU instructions (opcode = 1010011)
        imem[0] = 32'b0000000_00010_00001_000_10000_1010011;  // fadd.s f16, f1, f2
        imem[1] = 32'b0000100_00101_00100_000_10001_1010011;  // fsub.s f17, f4, f5
        imem[2] = 32'b0001000_01000_00111_000_10010_1010011;  // fmul.s f18, f7, f8
        imem[3] = 32'b0001100_01011_01010_000_10011_1010011;  // fdiv.s f19, f10, f11
        imem[4] = 32'h00000000;   // Terminate
        for (int i=5; i<1024; i++) imem[i] = 0;
    end
    
    always_ff @(posedge clk or posedge reset) begin
        if (reset) begin
            pc <= 0;
            instruction_out <= 0;
            valid_out <= 0;
            terminated <= 0;
        end
        else if (!stall && !terminated) begin
            instruction_out <= imem[pc >> 2];
            valid_out <= (imem[pc >> 2] != 0);
            
            if (imem[pc >> 2] == 32'h00000000) begin
                terminated <= 1;
                valid_out <= 0;
            end
            else begin
                pc <= pc + 4;
            end
        end
    end
    
    assign pc_out = pc;
endmodule

module Decode (
    input logic clk,
    input logic reset,
    input logic stall,
    input logic valid_in,
    input logic [31:0] instruction_in,
    input shortreal reg_file [0:31],
    
    output logic [31:0] op1_out,
    output logic [31:0] op2_out,
    output logic [4:0] rd_out,
    output logic reg_write_out,
    output logic valid_out,
    output logic [31:0] instruction_out
);
    always_ff @(posedge clk or posedge reset) begin
        if (reset) begin
            op1_out <= 0;
            op2_out <= 0;
            rd_out <= 0;
            reg_write_out <= 0;
            valid_out <= 0;
            instruction_out <= 0;
        end
        else if (!stall) begin
            valid_out <= valid_in;
            instruction_out <= instruction_in;
            
            if (valid_in && (instruction_in != 0)) begin
                op1_out <= $shortrealtobits(reg_file[(instruction_in >> 15) & 5'h1F]);
                op2_out <= $shortrealtobits(reg_file[(instruction_in >> 20) & 5'h1F]);
                rd_out <= (instruction_in >> 7) & 5'h1F;
                reg_write_out <= 1'b1;
            end
            else begin
                reg_write_out <= 0;
            end
        end
    end
endmodule

