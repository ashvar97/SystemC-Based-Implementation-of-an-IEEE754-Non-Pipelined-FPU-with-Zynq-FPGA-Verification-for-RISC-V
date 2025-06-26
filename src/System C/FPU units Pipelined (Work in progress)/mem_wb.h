SC_MODULE(Memory) {
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

        if (!reset.read() && !stall.read()) {
            result_out.write(result_in.read());
            rd_out.write(rd_in.read());
            reg_write_out.write(reg_write_in.read());
            valid_out.write(valid_in.read());
            instruction_out.write(instruction_in.read());

            if (valid_in.read()) {
                sc_uint<32> opcode = (instruction_in.read() >> 25) & 0x7F;
                cout << "MEM @" << sc_time_stamp() << ": ";
                cout << "rd=f" << rd_in.read();
                cout << " opcode=0x" << opcode << endl;
            }
        }
    }

    SC_CTOR(Memory) {
        SC_METHOD(memory_process);
        sensitive << reset << stall << valid_in << result_in << rd_in
                 << reg_write_in << instruction_in;
        // Initialize outputs
        result_out.initialize(0);
        rd_out.initialize(0);
        reg_write_out.initialize(false);
        valid_out.initialize(false);
        instruction_out.initialize(0);
    }
};

SC_MODULE(Writeback) {
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
        // Initialize outputs
        result_out.write(0);
        rd_out.write(0);
        reg_write_en.write(false);
        valid_out.write(false);

        if (!reset.read() && !stall.read()) {
            result_out.write(result_in.read());
            rd_out.write(rd_in.read());
            bool do_write = reg_write_in.read() && valid_in.read() && (instruction_in.read() != 0);
            reg_write_en.write(do_write);
            valid_out.write(valid_in.read());

            if (do_write) {
                sc_uint<32> opcode = (instruction_in.read() >> 25) & 0x7F;
                cout << "WB  @" << sc_time_stamp() << ": ";
                cout << " (opcode=0x" << opcode << ")" << endl;
            }
        }
    }

    SC_CTOR(Writeback) {
        SC_METHOD(writeback_process);
        sensitive << reset << stall << valid_in << result_in << rd_in
                 << reg_write_in << instruction_in;
        // Initialize outputs
        result_out.initialize(0);
        rd_out.initialize(0);
        reg_write_en.initialize(false);
        valid_out.initialize(false);
    }
};
