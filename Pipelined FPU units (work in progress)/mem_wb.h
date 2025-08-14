
SC_MODULE(Memory) {
    sc_in<bool> clk;
    sc_in<bool> reset;
    sc_in<bool> stall;
    sc_in<bool> valid_in;
    sc_in<sc_uint<32>> result_in;
    sc_in<sc_uint<5>> rd_in;
    sc_in<bool> reg_write_in;
    sc_in<sc_uint<32>> instruction_in;
    
    sc_out<sc_uint<32>> result_out;
    sc_out<sc_uint<5>> rd_out;
    sc_out<bool> reg_write_out;
    sc_out<bool> valid_out;
    sc_out<sc_uint<32>> instruction_out;
    
    void memory_process() {
        // Initialize outputs
        result_out.write(0);
        rd_out.write(0);
        reg_write_out.write(false);
        valid_out.write(false);
        instruction_out.write(0);
        
        wait();
        
        while (true) {
            if (reset.read()) {
                result_out.write(0);
                rd_out.write(0);
                reg_write_out.write(false);
                valid_out.write(false);
                instruction_out.write(0);
            } else if (!stall.read()) {
                // FIXED: Simple pass-through - no modifications
                result_out.write(result_in.read());
                rd_out.write(rd_in.read());
                reg_write_out.write(reg_write_in.read());
                valid_out.write(valid_in.read());
                instruction_out.write(instruction_in.read());
                
                if (valid_in.read() && reg_write_in.read()) {
                    sc_uint<32> instruction = instruction_in.read();
                    sc_uint<7> funct7 = instruction.range(31, 25);
                    cout << "MEM @" << sc_time_stamp() << ": ";
                    cout << "rd=f" << rd_in.read();
                    cout << " funct7=0x" << hex << funct7 << dec;
                    cout << " result=0x" << hex << result_in.read() << dec << endl;
                }
            }
            wait();
        }
    }
    
    SC_CTOR(Memory) {
        SC_CTHREAD(memory_process, clk.pos());
        reset_signal_is(reset, true);
        
        result_out.initialize(0);
        rd_out.initialize(0);
        reg_write_out.initialize(false);
        valid_out.initialize(false);
        instruction_out.initialize(0);
    }
};


SC_MODULE(Writeback) {
    sc_in<bool> clk;
    sc_in<bool> reset;
    sc_in<bool> stall;
    sc_in<bool> valid_in;
    sc_in<sc_uint<32>> result_in;
    sc_in<sc_uint<5>> rd_in;
    sc_in<bool> reg_write_in;
    sc_in<sc_uint<32>> instruction_in;
    
    sc_out<sc_uint<32>> result_out;
    sc_out<sc_uint<5>> rd_out;
    sc_out<bool> reg_write_en;
    sc_out<bool> valid_out;
    
    void writeback_process() {
        result_out.write(0);
        rd_out.write(0);
        reg_write_en.write(false);
        valid_out.write(false);
        
        wait();
        
        while (true) {
            if (reset.read()) {
                result_out.write(0);
                rd_out.write(0);
                reg_write_en.write(false);
                valid_out.write(false);
            } else if (!stall.read()) {
                // Pass through all data unchanged
                result_out.write(result_in.read());
                rd_out.write(rd_in.read());
                valid_out.write(valid_in.read());
                
                // Only enable write for valid FP operations
                sc_uint<32> instruction = instruction_in.read();
                sc_uint<7> base_opcode = instruction.range(6, 0);
                bool is_fp_instr = (base_opcode == 0x53) && (instruction != 0);
                bool do_write = reg_write_in.read() && valid_in.read() && is_fp_instr;
                
                reg_write_en.write(do_write);
            }
            wait();
        }
    }
    
    SC_CTOR(Writeback) {
        SC_CTHREAD(writeback_process, clk.pos());
        reset_signal_is(reset, true);
        
        result_out.initialize(0);
        rd_out.initialize(0);
        reg_write_en.initialize(false);
        valid_out.initialize(false);
    }
};


