module FPPipelinedProcessor_tb;
    logic clk;
    logic reset;
    logic stall;
    
    logic monitor_valid;
    logic [7:0] monitor_pc;
    
    FPPipelinedProcessor uut (
        .clk(clk),
        .reset(reset),
        .stall(stall),
        .monitor_valid(monitor_valid),
        .monitor_pc(monitor_pc)
    );
    
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end
    
    function automatic bit[31:0] floatToHex(real value);
        return $shortrealtobits(value);
    endfunction
    
    function automatic real hexToFloat(bit[31:0] value);
        return $bitstoshortreal(value);
    endfunction
    
    initial begin
        // Initialize instruction memory with test instructions
        // RISC-V Floating Point instruction format:
        // {funct7[31:25], rs2[24:20], rs1[19:15], funct3[14:12], rd[11:7], opcode[6:0]}
        // For FP operations, opcode is 0x53 (1010011)
        
        // Load first set of values into registers r1 and r2
        uut.reg_file[1] = floatToHex(3.14159);  // Pi in r1
        uut.reg_file[2] = floatToHex(2.71828);  // e in r2
        
        // Test 1: fadd.s r3, r1, r2 (funct7 = 0)
        uut.imem.imem[0] = {7'd0, 5'd2, 5'd1, 3'd0, 5'd3, 7'b1010011};
        
        // Test 2: fsub.s r4, r1, r2 (funct7 = 4)
        uut.imem.imem[1] = {7'd4, 5'd2, 5'd1, 3'd0, 5'd4, 7'b1010011};
        
        // Test 3: fmul.s r5, r1, r2 (funct7 = 8)
        uut.imem.imem[2] = {7'd8, 5'd2, 5'd1, 3'd0, 5'd5, 7'b1010011};
        
        // Test 4: fdiv.s r6, r1, r2 (funct7 = 12)
        uut.imem.imem[3] = {7'd12, 5'd2, 5'd1, 3'd0, 5'd6, 7'b1010011};
        
        // Edge cases
        // Store 1.0 in r7 and 0.0 in r8
        uut.reg_file[7] = floatToHex(1.0);
        uut.reg_file[8] = floatToHex(0.0);
        
        // Test 5: fdiv.s r9, r7, r8 (divide by zero)
        uut.imem.imem[4] = {7'd12, 5'd8, 5'd7, 3'd0, 5'd9, 7'b1010011};
        
        // Test 6: Special case - very large number
        uut.reg_file[10] = floatToHex(1.0e30);  // Very large number
        uut.reg_file[11] = floatToHex(1.0e-30); // Very small number
        
        // fmul.s r12, r10, r11 (should be close to 1.0)
        uut.imem.imem[5] = {7'd8, 5'd11, 5'd10, 3'd0, 5'd12, 7'b1010011};
        
        // Test 7: fadd.s r13, r10, r10 (large + large)
        uut.imem.imem[6] = {7'd0, 5'd10, 5'd10, 3'd0, 5'd13, 7'b1010011};
        
        // Additional test cases
        
        // Test 8: Store infinity in r14
        uut.reg_file[14] = 32'h7f800000; // Positive infinity
        
        // Test 9: Store NaN in r15
        uut.reg_file[15] = 32'h7fc00000; // NaN
        
        // Test 10: fmul.s r16, r1, r7 (Pi * 1.0 = Pi)
        uut.imem.imem[7] = {7'd8, 5'd7, 5'd1, 3'd0, 5'd16, 7'b1010011};
        
        // Test 14: fadd.s r20, r7, r14 (1.0 + infinity = infinity)
        uut.imem.imem[11] = {7'd0, 5'd14, 5'd7, 3'd0, 5'd20, 7'b1010011};
        
        // Test 11: fadd.s r17, r15, r1 (NaN + anything = NaN)
        uut.imem.imem[8] = {7'd0, 5'd1, 5'd15, 3'd0, 5'd17, 7'b1010011};
        
        // Test 12: fdiv.s r18, r1, r1 (number / itself = 1.0)
        uut.imem.imem[9] = {7'd12, 5'd1, 5'd1, 3'd0, 5'd18, 7'b1010011};
        
        // Test 13: fsub.s r19, r8, r8 (0 - 0 = 0)
        uut.imem.imem[10] = {7'd4, 5'd8, 5'd8, 3'd0, 5'd19, 7'b1010011};
        
        // Terminate with 0
        uut.imem.imem[12] = 32'b0;
    end
    
    real expected_values[9];
    
    initial begin
        expected_values[0] = 3.14159 + 2.71828;
        expected_values[1] = 3.14159 - 2.71828;
        expected_values[2] = 3.14159 * 2.71828;
        expected_values[3] = 3.14159 / 2.71828;
        expected_values[5] = $bitstoshortreal(32'h7fc00000) + 3.14159;
        expected_values[6] = 3.14159 / 3.14159;
        expected_values[7] = 0.0 - 0.0;
    end
    
    initial begin
        // Initialize inputs
        reset = 1;
        stall = 0;
        
        // Reset the system
        #20;
        reset = 0;
        
        // Let the system run for enough cycles to complete all instructions
        #1000;
        
        // Display results
        $display("==== Test Results ====");
        $display("Register file contents:");
        
        print_registers();
        verify_results();
        print_special_cases();
        print_additional_tests();
        
        $finish;
    end
    
    task print_registers();
        integer i;
        begin
            for (i = 0; i < 32; i = i + 1) begin
                if (uut.reg_file[i] != 0) begin
                    $display("r%0d = %h (float: %f)", i, uut.reg_file[i], hexToFloat(uut.reg_file[i]));
                end
            end
        end
    endtask
    
    task verify_results();
        begin
            $display("\n==== Basic Operations Verification ====");
            $display("r3 (add): %f (expected: %f), diff: %f", 
                     hexToFloat(uut.reg_file[3]), expected_values[0], 
                     hexToFloat(uut.reg_file[3]) - expected_values[0]);
            
            $display("r4 (sub): %f (expected: %f), diff: %f", 
                     hexToFloat(uut.reg_file[4]), expected_values[1], 
                     hexToFloat(uut.reg_file[4]) - expected_values[1]);
            
            $display("r5 (mul): %f (expected: %f), diff: %f", 
                     hexToFloat(uut.reg_file[5]), expected_values[2], 
                     hexToFloat(uut.reg_file[5]) - expected_values[2]);
            
            $display("r6 (div): %f (expected: %f), diff: %f", 
                     hexToFloat(uut.reg_file[6]), expected_values[3], 
                     hexToFloat(uut.reg_file[6]) - expected_values[3]);
        end
    endtask
    
    task print_special_cases();
        begin
            $display("\n==== Special Cases ====");
            $display("r9 (div by zero): %h", uut.reg_file[9]);
            $display("r12 (very large * very small): %f", hexToFloat(uut.reg_file[12]));
            $display("r13 (large + large): %e", hexToFloat(uut.reg_file[13]));
        end
    endtask
    
    task print_additional_tests();
        begin
            $display("\n==== Additional Tests ====");
            $display("r16 (Pi * 1.0): %h (float: %f)", uut.reg_file[16], hexToFloat(uut.reg_file[16]));
            $display("  Expected: Pi = %f", hexToFloat(uut.reg_file[1]));
            
            $display("r17 (NaN + Pi): %h", uut.reg_file[17]);
            
            // Check if result is NaN
            if ((uut.reg_file[17] & 32'h7F800000) == 32'h7F800000 && (uut.reg_file[17] & 32'h007FFFFF) != 0)
                $display("  Correctly propagated NaN");
            
            $display("r18 (Pi / Pi): %f (expected: 1.0)", hexToFloat(uut.reg_file[18]));
            $display("r19 (0 - 0): %f (expected: 0.0)", hexToFloat(uut.reg_file[19]));
            $display("r20 (1.0 + infinity): %h", uut.reg_file[20]);
            
            // Check if result is infinity
            if (uut.reg_file[20] == 32'h7f800000)
                $display("  Correctly produced infinity");
        end
    endtask
    
    integer cycle_count;
    
    initial begin
        cycle_count = 0;
    end
    
    always @(posedge clk) begin
        if (!reset) begin
            cycle_count = cycle_count + 1;
            
            if ((cycle_count % 5 == 0) || (cycle_count < 10)) begin
                print_pipeline_state();
            end
            
            if (uut.wb_valid_out && uut.wb_reg_write_en) begin
                $display("  Completed write to r%0d = %h (%f)", 
                         uut.wb_rd_out, uut.wb_result_out, 
                         hexToFloat(uut.wb_result_out));
            end
        end
    end
    
    task print_pipeline_state();
        begin
            $display("\nCycle %0d:", cycle_count);
            
            $display("  IFU: pc=%h, instr=%h, valid=%b", 
                     uut.pc_out, uut.ifu_instruction_out, uut.ifu_valid_out);
                     
            $display("  DECODE: op1=%h, op2=%h, rd=%d, valid=%b, instr=%h", 
                     uut.op1_out, uut.op2_out, uut.rd_out, 
                     uut.decode_valid_out, uut.decode_instruction_out);
                     
            $display("  EXECUTE: result=%h (%f), rd=%d, valid=%b", 
                     uut.ex_result_out, hexToFloat(uut.ex_result_out),
                     uut.ex_rd_out, uut.ex_valid_out);
                     
            $display("  MEMORY: result=%h (%f), rd=%d, valid=%b", 
                     uut.mem_result_out, hexToFloat(uut.mem_result_out),
                     uut.mem_rd_out, uut.mem_valid_out);
                     
            $display("  WRITEBACK: result=%h (%f), rd=%d, valid=%b", 
                     uut.wb_result_out, hexToFloat(uut.wb_result_out),
                     uut.wb_rd_out, uut.wb_valid_out);
                     
            $display("  Monitor: valid=%b, pc=%h", monitor_valid, monitor_pc);
        end
    endtask
endmodule