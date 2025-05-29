`timescale 1ns/1ps

module Top_tb;

    // Inputs
    logic clk;
    logic reset;
    logic stall;
    
    // Outputs
    logic monitor_valid;
    logic [7:0] monitor_pc;
    
    // Interface signals for initialization
    logic [31:0] init_result;
    logic [4:0] init_rd;
    logic init_reg_write;
    logic init_valid;
    
    logic [31:0] imem_addr;
    logic [31:0] imem_instr;
    
    // Instantiate the Unit Under Test (UUT)
    FPPipelinedProcessor uut (
        .clk(clk),
        .reset(reset),
        .stall(stall),
        .monitor_valid(monitor_valid),
        .monitor_pc(monitor_pc)
    );
    
    // Clock generation
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end
    
    // Helper function to create FP instructions
    function [31:0] createFPInstruction(input [6:0] funct7, input [4:0] rs2, input [4:0] rs1, input [4:0] rd);
        return {funct7, rs2, rs1, 3'b000, rd, 7'b1010011};
    endfunction
    
    // Test sequence
    initial begin
        // Initialize inputs
        reset = 1;
        stall = 1;
        init_result = 0;
        init_rd = 0;
        init_reg_write = 0;
        init_valid = 0;
        imem_addr = 0;
        imem_instr = 0;
        
        // Apply reset
        #20;
        reset = 0;
        
        // Wait a bit before starting
        #20;
        
        // Force the writeback values to initialize registers
        // r1 = Pi (3.14159)
        init_result = 32'h4048F5C3;
        init_rd = 1;
        init_reg_write = 1;
        init_valid = 1;
        
        force uut.wb_result_out = init_result;
        force uut.wb_rd_out = init_rd;
        force uut.wb_reg_write_en = init_reg_write;
        force uut.wb_valid_out = init_valid;
        @(posedge clk);
        
        // r2 = e (2.71828)
        init_result = 32'h402DF854;
        init_rd = 2;
        force uut.wb_result_out = init_result;
        force uut.wb_rd_out = init_rd;
        @(posedge clk);
        
        // r7 = 1.0
        init_result = 32'h3F800000;
        init_rd = 7;
        force uut.wb_result_out = init_result;
        force uut.wb_rd_out = init_rd;
        @(posedge clk);
        
        // r8 = 0.0
        init_result = 32'h00000000;
        init_rd = 8;
        force uut.wb_result_out = init_result;
        force uut.wb_rd_out = init_rd;
        @(posedge clk);
        
        // r10 = Very large (1.0e30)
        init_result = 32'h7E967699;
        init_rd = 10;
        force uut.wb_result_out = init_result;
        force uut.wb_rd_out = init_rd;
        @(posedge clk);
        
        // r11 = Very small (1.0e-30)
        init_result = 32'h0160B8CB;
        init_rd = 11;
        force uut.wb_result_out = init_result;
        force uut.wb_rd_out = init_rd;
        @(posedge clk);
        
        // r14 = Positive infinity
        init_result = 32'h7F800000;
        init_rd = 14;
        force uut.wb_result_out = init_result;
        force uut.wb_rd_out = init_rd;
        @(posedge clk);
        
        // r15 = NaN
        init_result = 32'h7FC00000;
        init_rd = 15;
        force uut.wb_result_out = init_result;
        force uut.wb_rd_out = init_rd;
        @(posedge clk);
        
        // Turn off register writing
        init_reg_write = 0;
        init_valid = 0;
        force uut.wb_reg_write_en = init_reg_write;
        force uut.wb_valid_out = init_valid;
        @(posedge clk);
        
        // Release the writeback signals
        release uut.wb_result_out;
        release uut.wb_rd_out;
        release uut.wb_reg_write_en;
        release uut.wb_valid_out;
        
        // Now load instruction memory using force/release on imem signals
        // fadd.s r3, r1, r2
        imem_addr = 0;
        imem_instr = createFPInstruction(7'h00, 5'd2, 5'd1, 5'd3);
        force uut.imem_address = imem_addr;
        force uut.imem_instruction = imem_instr;
        #2;
        
        // fsub.s r4, r1, r2
        imem_addr = 4;
        imem_instr = createFPInstruction(7'h04, 5'd2, 5'd1, 5'd4);
        force uut.imem_address = imem_addr;
        force uut.imem_instruction = imem_instr;
        #2;
        
        // fmul.s r5, r1, r2
        imem_addr = 8;
        imem_instr = createFPInstruction(7'h08, 5'd2, 5'd1, 5'd5);
        force uut.imem_address = imem_addr;
        force uut.imem_instruction = imem_instr;
        #2;
        
        // fdiv.s r6, r1, r2
        imem_addr = 12;
        imem_instr = createFPInstruction(7'h0C, 5'd2, 5'd1, 5'd6);
        force uut.imem_address = imem_addr;
        force uut.imem_instruction = imem_instr;
        #2;
        
        // fdiv.s r9, r7, r8 (div by zero)
        imem_addr = 16;
        imem_instr = createFPInstruction(7'h0C, 5'd8, 5'd7, 5'd9);
        force uut.imem_address = imem_addr;
        force uut.imem_instruction = imem_instr;
        #2;
        
        // fmul.s r12, r10, r11 (very large * very small)
        imem_addr = 20;
        imem_instr = createFPInstruction(7'h08, 5'd11, 5'd10, 5'd12);
        force uut.imem_address = imem_addr;
        force uut.imem_instruction = imem_instr;
        #2;
        
        // fadd.s r13, r10, r10 (very large + very large)
        imem_addr = 24;
        imem_instr = createFPInstruction(7'h00, 5'd10, 5'd10, 5'd13);
        force uut.imem_address = imem_addr;
        force uut.imem_instruction = imem_instr;
        #2;
        
        // fmul.s r16, r1, r7 (Pi * 1.0)
        imem_addr = 28;
        imem_instr = createFPInstruction(7'h08, 5'd7, 5'd1, 5'd16);
        force uut.imem_address = imem_addr;
        force uut.imem_instruction = imem_instr;
        #2;
        
        // fadd.s r17, r15, r1 (NaN + Pi)
        imem_addr = 32;
        imem_instr = createFPInstruction(7'h00, 5'd1, 5'd15, 5'd17);
        force uut.imem_address = imem_addr;
        force uut.imem_instruction = imem_instr;
        #2;
        
        // fdiv.s r18, r1, r1 (Pi / Pi)
        imem_addr = 36;
        imem_instr = createFPInstruction(7'h0C, 5'd1, 5'd1, 5'd18);
        force uut.imem_address = imem_addr;
        force uut.imem_instruction = imem_instr;
        #2;
        
        // fsub.s r19, r8, r8 (0.0 - 0.0)
        imem_addr = 40;
        imem_instr = createFPInstruction(7'h04, 5'd8, 5'd8, 5'd19);
        force uut.imem_address = imem_addr;
        force uut.imem_instruction = imem_instr;
        #2;
        
        // fadd.s r20, r7, r14 (1.0 + infinity)
        imem_addr = 44;
        imem_instr = createFPInstruction(7'h00, 5'd14, 5'd7, 5'd20);
        force uut.imem_address = imem_addr;
        force uut.imem_instruction = imem_instr;
        #2;
        
        // End of program
        imem_addr = 48;
        imem_instr = 32'h0;
        force uut.imem_address = imem_addr;
        force uut.imem_instruction = imem_instr;
        #2;
        
        // Release the imem signals
        release uut.imem_address;
        release uut.imem_instruction;
        
        // Load instruction memory through the imem module's array directly
        force uut.imem.imem[0] = createFPInstruction(7'h00, 5'd2, 5'd1, 5'd3);   // fadd.s r3, r1, r2
        force uut.imem.imem[1] = createFPInstruction(7'h04, 5'd2, 5'd1, 5'd4);   // fsub.s r4, r1, r2
        force uut.imem.imem[2] = createFPInstruction(7'h08, 5'd2, 5'd1, 5'd5);   // fmul.s r5, r1, r2
        force uut.imem.imem[3] = createFPInstruction(7'h0C, 5'd2, 5'd1, 5'd6);   // fdiv.s r6, r1, r2
        force uut.imem.imem[4] = createFPInstruction(7'h0C, 5'd8, 5'd7, 5'd9);   // fdiv.s r9, r7, r8 (div by zero)
        force uut.imem.imem[5] = createFPInstruction(7'h08, 5'd11, 5'd10, 5'd12); // fmul.s r12, r10, r11 (large * small)
        force uut.imem.imem[6] = createFPInstruction(7'h00, 5'd10, 5'd10, 5'd13); // fadd.s r13, r10, r10 (large + large)
        force uut.imem.imem[7] = createFPInstruction(7'h08, 5'd7, 5'd1, 5'd16);  // fmul.s r16, r1, r7 (Pi * 1.0)
        force uut.imem.imem[8] = createFPInstruction(7'h00, 5'd1, 5'd15, 5'd17); // fadd.s r17, r15, r1 (NaN + Pi)
        force uut.imem.imem[9] = createFPInstruction(7'h0C, 5'd1, 5'd1, 5'd18);  // fdiv.s r18, r1, r1 (Pi / Pi)
        force uut.imem.imem[10] = createFPInstruction(7'h04, 5'd8, 5'd8, 5'd19); // fsub.s r19, r8, r8 (0.0 - 0.0)
        force uut.imem.imem[11] = createFPInstruction(7'h00, 5'd14, 5'd7, 5'd20); // fadd.s r20, r7, r14 (1.0 + inf)
        force uut.imem.imem[12] = 32'h0; // End of program
        
        // Start execution
        stall = 0;
        
        // Let the processor run for a while
        #500;
        
        // Display results from register file for verification
        $display("\n==== Test Results ====");
        $display("Register file contents:");
        
        $display("\n==== Basic Operations ====");
        $display("r3 (Pi + e):      %h (%f)", uut.reg_file[3], $bitstoshortreal(uut.reg_file[3]));
        $display("r4 (Pi - e):      %h (%f)", uut.reg_file[4], $bitstoshortreal(uut.reg_file[4]));
        $display("r5 (Pi * e):      %h (%f)", uut.reg_file[5], $bitstoshortreal(uut.reg_file[5]));
        $display("r6 (Pi / e):      %h (%f)", uut.reg_file[6], $bitstoshortreal(uut.reg_file[6]));
        
        $display("\n==== Special Cases ====");
        $display("r9 (1.0 / 0.0):   %h", uut.reg_file[9]);
        $display("r12 (large * small): %h (%f)", uut.reg_file[12], $bitstoshortreal(uut.reg_file[12]));
        $display("r13 (large + large): %h", uut.reg_file[13]);
        
        $display("\n==== Additional Tests ====");
        $display("r16 (Pi * 1.0):   %h (%f)", uut.reg_file[16], $bitstoshortreal(uut.reg_file[16]));
        $display("r17 (NaN + Pi):   %h", uut.reg_file[17]);
        $display("r18 (Pi / Pi):    %h (%f)", uut.reg_file[18], $bitstoshortreal(uut.reg_file[18]));
        $display("r19 (0.0 - 0.0):  %h (%f)", uut.reg_file[19], $bitstoshortreal(uut.reg_file[19]));
        $display("r20 (1.0 + inf):  %h", uut.reg_file[20]);
        
        // End simulation
        #100;
        $finish;
    end
    
    // Waveform dumping - focusing only on important signals
    initial begin
        $dumpfile("fp_system.vcd");
        $dumpvars(0, Top_tb);
        
        // Core control signals
        $dumpvars(0, clk);
        $dumpvars(0, reset);
        $dumpvars(0, stall);
        $dumpvars(0, monitor_valid);
        $dumpvars(0, monitor_pc);
        
        // Pipeline stage signals
        $dumpvars(0, uut.pc_out);
        $dumpvars(0, uut.ifu_instruction_out);
        $dumpvars(0, uut.ifu_valid_out);
        
        // Internal registers
        $dumpvars(0, uut.reg_file[1]);  // Pi
        $dumpvars(0, uut.reg_file[2]);  // e
        $dumpvars(0, uut.reg_file[3]);  // Pi + e
        $dumpvars(0, uut.reg_file[4]);  // Pi - e
        $dumpvars(0, uut.reg_file[5]);  // Pi * e
        $dumpvars(0, uut.reg_file[6]);  // Pi / e
        $dumpvars(0, uut.reg_file[7]);  // 1.0
        $dumpvars(0, uut.reg_file[8]);  // 0.0
        $dumpvars(0, uut.reg_file[9]);  // 1.0 / 0.0
        $dumpvars(0, uut.reg_file[10]); // Very large
        $dumpvars(0, uut.reg_file[11]); // Very small
        $dumpvars(0, uut.reg_file[12]); // large * small
        $dumpvars(0, uut.reg_file[13]); // large + large
        $dumpvars(0, uut.reg_file[14]); // Infinity
        $dumpvars(0, uut.reg_file[15]); // NaN
        $dumpvars(0, uut.reg_file[16]); // Pi * 1.0
        $dumpvars(0, uut.reg_file[17]); // NaN + Pi
        $dumpvars(0, uut.reg_file[18]); // Pi / Pi
        $dumpvars(0, uut.reg_file[19]); // 0.0 - 0.0
        $dumpvars(0, uut.reg_file[20]); // 1.0 + infinity
        
        // Writeback interface
        $dumpvars(0, uut.wb_result_out);
        $dumpvars(0, uut.wb_rd_out);
        $dumpvars(0, uut.wb_reg_write_en);
        $dumpvars(0, uut.wb_valid_out);
        
        // FPU operation results (from execute module)
        $dumpvars(0, uut.execute.fp_add_result);
        $dumpvars(0, uut.execute.fp_sub_result);
        $dumpvars(0, uut.execute.fp_mul_result);
        $dumpvars(0, uut.execute.fp_div_result);
    end
    
endmodule
