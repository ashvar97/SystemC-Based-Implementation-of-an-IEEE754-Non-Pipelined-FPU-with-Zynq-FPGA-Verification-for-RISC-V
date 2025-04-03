module testbench;
    logic clk = 0;
    logic reset = 1;
    logic stall = 0;
    logic [31:0] monitor_pc;
    logic [31:0] monitor_instruction;
    logic monitor_valid;

    always #5 clk = ~clk;

    Top dut (
        .clk(clk),
        .reset(reset),
        .stall(stall),
        .monitor_pc(monitor_pc),
        .monitor_instruction(monitor_instruction),
        .monitor_valid(monitor_valid)
    );
    
    function void print_reg_file();
        $display("\nRegister File Contents:");
        $display("f1:  %h (5.5 expected)", dut.reg_file[1]);
        $display("f2:  %h (2.5 expected)", dut.reg_file[2]);
        $display("f4:  %h (10.0 expected)", dut.reg_file[4]);
        $display("f5:  %h (3.0 expected)", dut.reg_file[5]);
        $display("f7:  %h (4.0 expected)", dut.reg_file[7]);
        $display("f8:  %h (2.5 expected)", dut.reg_file[8]);
        $display("f10: %h (15.0 expected)", dut.reg_file[10]);
        $display("f11: %h (3.0 expected)", dut.reg_file[11]);
        $display("f16: %h (8.0 expected - 5.5+2.5)", dut.reg_file[16]);
        $display("f17: %h (7.0 expected - 10.0-3.0)", dut.reg_file[17]);
        $display("f18: %h (10.0 expected - 4.0*2.5)", dut.reg_file[18]);
        $display("f19: %h (5.0 expected - 15.0/3.0)", dut.reg_file[19]);
    endfunction
    
    initial begin
        dut.reg_file[1]  = 32'h40B00000;
        dut.reg_file[2]  = 32'h40200000;
        dut.reg_file[4]  = 32'h41200000;
        dut.reg_file[5]  = 32'h40400000;
        dut.reg_file[7]  = 32'h40800000;
        dut.reg_file[8]  = 32'h40200000;
        dut.reg_file[10] = 32'h41700000;
        dut.reg_file[11] = 32'h40400000;
        
        $dumpfile("fpu_pipeline.vcd");
        $dumpvars(0, testbench);
        
        #10 reset = 0;
        $display("Reset released at %0t", $time);
        
        $display("\nTime\tPC\t\tInstruction\tValid");
        $monitor("%0t\t%h\t%h\t%b", $time, monitor_pc, monitor_instruction, monitor_valid);
        
        #200;
        print_reg_file();
        
        if (dut.reg_file[16] != 32'h41000000) $error("f16 (5.5+2.5) incorrect");
        if (dut.reg_file[17] != 32'h40E00000) $error("f17 (10.0-3.0) incorrect");
        if (dut.reg_file[18] != 32'h41200000) $error("f18 (4.0*2.5) incorrect");
        if (dut.reg_file[19] != 32'h40A00000) $error("f19 (15.0/3.0) incorrect");
        
        $display("\nTest completed at %0t", $time);
        $finish;
    end
    
    initial begin
        #75  stall = 1;
        #10  stall = 0;
        #125 stall = 1;
        #15  stall = 0;
    end
endmodule
