SC_MODULE(Execute) {
    // Port declarations
    sc_in<bool> clk;
    sc_in<bool> reset;
    sc_in<bool> stall;
    sc_in<bool> valid_in;
    sc_in<sc_uint<32>> op1;
    sc_in<sc_uint<32>> op2;
    sc_in<sc_uint<7>> opcode;
    sc_in<sc_uint<5>> rd_in;
    sc_in<bool> reg_write_in;
    sc_in<sc_uint<32>> instruction_in;
    
    sc_out<sc_uint<32>> result_out;
    sc_out<sc_uint<5>> rd_out;
    sc_out<bool> reg_write_out;
    sc_out<bool> valid_out;
    sc_out<sc_uint<32>> instruction_out;

    // Internal signals
    sc_signal<sc_uint<32>> fp_add_result;
    sc_signal<sc_uint<32>> fp_sub_result;
    sc_signal<sc_uint<32>> fp_mul_result;
    sc_signal<sc_uint<32>> fp_div_result;
    sc_signal<bool> sub_enable;


    // Submodules
    ieee754_adder* fp_adder;
    ieee754_subtractor* fp_subtractor;
    ieee754mult* fp_multiplier;
    ieee754_div* fp_divider;

    void execute_process() {
        // Initialize constants
        sub_enable.write(true);
        wait();

        while (true) {
            if (reset.read()) {
                result_out.write(0);
                rd_out.write(0);
                reg_write_out.write(false);
                valid_out.write(false);
                instruction_out.write(0);
            }
            else if (!stall.read()) {
                valid_out.write(valid_in.read());
                rd_out.write(rd_in.read());
                reg_write_out.write(reg_write_in.read());
                instruction_out.write(instruction_in.read());

                if (valid_in.read() && reg_write_in.read()) {
                    switch(opcode.read()) {
                        case 0x00: result_out.write(fp_add_result.read()); break;
                        case 0x04: result_out.write(fp_sub_result.read()); break;
                        case 0x08: result_out.write(fp_mul_result.read()); break;
                        case 0x0C: result_out.write(fp_div_result.read()); break;
                        default: result_out.write(0); break;
                    }
                }
            }
            wait();
        }
    }

    SC_CTOR(Execute) : sub_enable("sub_enable"){
        fp_adder = new ieee754_adder("fp_adder");
        fp_adder->A(op1);
        fp_adder->B(op2);
        fp_adder->O(fp_add_result);

        fp_subtractor = new ieee754_subtractor("fp_subtractor");
        fp_subtractor->a(op1);
        fp_subtractor->b(op2);
        fp_subtractor->enable(sub_enable);
        fp_subtractor->ans(fp_sub_result);

        fp_multiplier = new ieee754mult("fp_multiplier");
        fp_multiplier->A(op1);
        fp_multiplier->B(op2);
        fp_multiplier->reset(reset);
        fp_multiplier->result(fp_mul_result);

        fp_divider = new ieee754_div("fp_divider");
        fp_divider->a(op1);
        fp_divider->b(op2);
        fp_divider->reset(reset);
        fp_divider->result(fp_div_result);

        SC_CTHREAD(execute_process, clk.pos());
        reset_signal_is(reset, true);

        // Initialize outputs
        result_out.initialize(0);
        rd_out.initialize(0);
        reg_write_out.initialize(false);
        valid_out.initialize(false);
        instruction_out.initialize(0);
    }

    ~Execute() {
        delete fp_adder;
        delete fp_subtractor;
        delete fp_multiplier;
        delete fp_divider;
    }
};
