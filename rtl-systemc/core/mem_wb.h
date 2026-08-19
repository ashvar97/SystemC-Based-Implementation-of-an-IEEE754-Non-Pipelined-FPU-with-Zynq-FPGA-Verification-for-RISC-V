SC_MODULE(Memory) {
    sc_in<bool> clk;  // ✅ NOW CLOCKED
    sc_in<bool> reset;
    sc_in<bool> stall;
    sc_in<bool> valid_in;
    sc_in<sc_uint<32>> result_in;
    sc_in<sc_uint<5>> rd_in;
    sc_in<bool> reg_write_in;
    sc_in<sc_uint<32>> instruction_in;
    sc_in<sc_uint<8>> pc_in;  // ✅ NEW: PC input
    
    sc_out<sc_uint<32>> result_out;
    sc_out<sc_uint<5>> rd_out;
    sc_out<bool> reg_write_out;
    sc_out<bool> valid_out;
    sc_out<sc_uint<32>> instruction_out;
    sc_out<sc_uint<8>> pc_out;  // ✅ NEW: PC output
    
    void memory_process() {
        // ✅ NOW SEQUENTIAL
        while (true) {
            if (reset.read()) {
                result_out.write(0);
                rd_out.write(0);
                reg_write_out.write(false);
                valid_out.write(false);
                instruction_out.write(0);
                pc_out.write(0);
            } 
            else if (!stall.read()) {
                result_out.write(result_in.read());
                rd_out.write(rd_in.read());
                reg_write_out.write(reg_write_in.read());
                valid_out.write(valid_in.read());
                instruction_out.write(instruction_in.read());
                pc_out.write(pc_in.read());  // ✅ Propagate PC
                
                if (valid_in.read()) {
                    sc_uint<32> opcode = (instruction_in.read() >> 25) & 0x7F;
                    cout << "MEM @" << sc_time_stamp() << ": ";
                    cout << "PC=0x" << hex << pc_in.read() << dec;
                    cout << " rd=f" << rd_in.read();
                    cout << " opcode=0x" << hex << opcode << dec << endl;
                }
            }
            wait();
        }
    }
    
    SC_CTOR(Memory) {
        SC_CTHREAD(memory_process, clk.pos());  // ✅ NOW CLOCKED
        reset_signal_is(reset, true);
        
        result_out.initialize(0);
        rd_out.initialize(0);
        reg_write_out.initialize(false);
        valid_out.initialize(false);
        instruction_out.initialize(0);
        pc_out.initialize(0);
    }
};

SC_MODULE(Writeback) {
    sc_in<bool> clk;  // ✅ NOW CLOCKED
    sc_in<bool> reset;
    sc_in<bool> stall;
    sc_in<bool> valid_in;
    sc_in<sc_uint<32>> result_in;
    sc_in<sc_uint<5>> rd_in;
    sc_in<bool> reg_write_in;
    sc_in<sc_uint<32>> instruction_in;
    sc_in<sc_uint<8>> pc_in;  // ✅ NEW: PC input
    
    sc_out<sc_uint<32>> result_out;
    sc_out<sc_uint<5>> rd_out;
    sc_out<bool> reg_write_en;
    sc_out<bool> valid_out;
    sc_out<sc_uint<8>> pc_out;  // ✅ NEW: PC output
    
    void writeback_process() {
        // ✅ NOW SEQUENTIAL
        while (true) {
            if (reset.read()) {
                result_out.write(0);
                rd_out.write(0);
                reg_write_en.write(false);
                valid_out.write(false);
                pc_out.write(0);
            }
            else if (!stall.read()) {
                result_out.write(result_in.read());
                rd_out.write(rd_in.read());
                bool do_write = reg_write_in.read() && valid_in.read() && (instruction_in.read() != 0);
                reg_write_en.write(do_write);
                valid_out.write(valid_in.read());
                pc_out.write(pc_in.read());  // ✅ Propagate PC
                
                if (do_write) {
                    sc_uint<32> opcode = (instruction_in.read() >> 25) & 0x7F;
                    cout << "WB  @" << sc_time_stamp() << ": ";
                    cout << "PC=0x" << hex << pc_in.read() << dec;
                    cout << " rd=f" << rd_in.read();
                    cout << " result=0x" << hex << result_in.read() << dec;
                    cout << " (opcode=0x" << hex << opcode << dec << ")" << endl;
                }
            }
            wait();
        }
    }
    
    SC_CTOR(Writeback) {
        SC_CTHREAD(writeback_process, clk.pos());  // ✅ NOW CLOCKED
        reset_signal_is(reset, true);
        
        result_out.initialize(0);
        rd_out.initialize(0);
        reg_write_en.initialize(false);
        valid_out.initialize(false);
        pc_out.initialize(0);
    }
};
