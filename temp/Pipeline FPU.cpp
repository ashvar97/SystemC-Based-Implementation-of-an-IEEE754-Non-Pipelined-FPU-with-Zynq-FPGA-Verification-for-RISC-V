#include <systemc.h>
#include <cstring>
#include <iostream>
#include "IEEE754Add.h"
#include "IEEE754Div.h"
#include "IEEE754Mult.h"
#include "IEEE754Sub.h"



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
// IEEE 754 Floating-point constants
const uint32_t FLOAT_5_5  = 0x40B00000; // 5.5
const uint32_t FLOAT_2_5  = 0x40200000; // 2.5
const uint32_t FLOAT_10_0 = 0x41200000; // 10.0
const uint32_t FLOAT_3_0  = 0x40400000; // 3.0
const uint32_t FLOAT_4_0  = 0x40800000; // 4.0
const uint32_t FLOAT_15_0 = 0x41700000; // 15.0
const uint32_t FLOAT_8_0  = 0x41000000; // 8.0
const uint32_t FLOAT_7_0  = 0x40E00000; // 7.0
const uint32_t FLOAT_5_0  = 0x40A00000; // 5.0

// Helper function for float-to-bits conversion
uint32_t float_to_bits(float f) {
    uint32_t result;
    memcpy(&result, &f, sizeof(result));
    return result;
}

SC_MODULE(IFU) {
    sc_in<bool> clk;
    sc_in<bool> reset;
    sc_in<bool> stall;
    sc_out<sc_uint<32>> pc_out;
    sc_out<sc_uint<32>> instruction_out;
    sc_out<bool> valid_out;

    sc_uint<32> pc;
    sc_uint<32> imem[1024];
    bool terminated;

    void init_imem() {
        // Initialize instruction memory to match Verilog version
        imem[0] = 0b00000000001000001000100001010011;  // fadd.s f16, f1, f2
        imem[1] = 0b00001000010100100000100011010011;  // fsub.s f17, f4, f5
        imem[2] = 0b00010000100000111000100101010011;  // fmul.s f18, f7, f8
        imem[3] = 0b00011000101101010000100111010011;  // fdiv.s f19, f10, f11
        imem[4] = 0x00000000;                          // Terminate
        for (int i = 5; i < 1024; i++) imem[i] = 0;
    }

    void fetch_process() {
        // Initialize memory
        init_imem();
        
        // Reset state
        pc = 0;
        terminated = false;
        instruction_out.write(0);
        valid_out.write(false);
        pc_out.write(0);
        wait();

        while (true) {
            if (reset.read()) {
                pc = 0;
                terminated = false;
                instruction_out.write(0);
                valid_out.write(false);
                pc_out.write(0);
            }
            else if (!stall.read() && !terminated) {
                // Word-aligned access
                sc_uint<32> current_pc = pc;
                sc_uint<32> instruction = imem[current_pc.range(31,2)];
                
                instruction_out.write(instruction);
                valid_out.write(instruction != 0);
                
                if (instruction == 0) {
                    terminated = true;
                    valid_out.write(false);
                }
                
                pc = current_pc + 4;
                pc_out.write(current_pc);  // Output the PC before increment
            }
            wait();
        }
    }

    SC_CTOR(IFU) {
        SC_CTHREAD(fetch_process, clk.pos());
        reset_signal_is(reset, true);
        
        // Initialize outputs
        pc_out.initialize(0);
        instruction_out.initialize(0);
        valid_out.initialize(false);
    }
};

SC_MODULE(Decode) {
    sc_in<bool> reset;
    sc_in<bool> stall;
    sc_in<bool> valid_in;
    sc_in<sc_uint<32>> instruction_in;
    sc_in<sc_uint<32>> reg_file[32];

    sc_out<sc_uint<32>> op1_out;
    sc_out<sc_uint<32>> op2_out;
    sc_out<sc_uint<5>> rd_out;
    sc_out<bool> reg_write_out;
    sc_out<bool> valid_out;
    sc_out<sc_uint<32>> instruction_out;

    void decode_process() {
        // Initialize outputs
        op1_out.write(0);
        op2_out.write(0);
        rd_out.write(0);
        reg_write_out.write(false);
        valid_out.write(false);
        instruction_out.write(0);

        if (!reset.read() && !stall.read()) {
            valid_out.write(valid_in.read());
            instruction_out.write(instruction_in.read());

            if (valid_in.read() && (instruction_in.read() != 0)) {
                sc_uint<5> rs1 = (instruction_in.read() >> 15) & 0x1F;
                sc_uint<5> rs2 = (instruction_in.read() >> 20) & 0x1F;

                if (rs1 < 32) op1_out.write(reg_file[rs1].read());
                if (rs2 < 32) op2_out.write(reg_file[rs2].read());
                rd_out.write((instruction_in.read() >> 7) & 0x1F);
                reg_write_out.write(true);

                cout << "DEC @" << sc_time_stamp() << ": ";
                cout << "rs1=f" << rs1 << " (0x" << hex << reg_file[rs1].read() << ") ";
                cout << "rs2=f" << rs2 << " (0x" << reg_file[rs2].read() << ") ";
                cout << "rd=f" << ((instruction_in.read() >> 7) & 0x1F) << endl;
            }
        }
    }

    SC_CTOR(Decode) {
        SC_METHOD(decode_process);
        sensitive << reset << stall << valid_in << instruction_in;
        for (int i = 0; i < 32; i++) {
            sensitive << reg_file[i];
        }
        // Initialize outputs
        op1_out.initialize(0);
        op2_out.initialize(0);
        rd_out.initialize(0);
        reg_write_out.initialize(false);
        valid_out.initialize(false);
        instruction_out.initialize(0);
    }
};


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

SC_MODULE(Top) {
    sc_in<bool> clk;
    sc_in<bool> reset;
    sc_in<bool> stall;
    sc_out<sc_uint<32>> monitor_pc;
    sc_out<sc_uint<32>> monitor_instruction;
    sc_out<bool> monitor_valid;

    // Pipeline signals
    sc_signal<sc_uint<32>> pc_out;
    sc_signal<sc_uint<32>> ifu_instruction_out;
    sc_signal<bool> ifu_valid_out;

    sc_signal<sc_uint<32>> op1_out, op2_out;
    sc_signal<sc_uint<5>> rd_out;
    sc_signal<bool> reg_write_out;
    sc_signal<bool> decode_valid_out;
    sc_signal<sc_uint<32>> decode_instruction_out;

    sc_signal<sc_uint<7>> opcode;
    sc_signal<sc_uint<32>> ex_result_out;
    sc_signal<sc_uint<5>> ex_rd_out;
    sc_signal<bool> ex_reg_write_out;
    sc_signal<bool> ex_valid_out;
    sc_signal<sc_uint<32>> ex_instruction_out;

    sc_signal<sc_uint<32>> mem_result_out;
    sc_signal<sc_uint<5>> mem_rd_out;
    sc_signal<bool> mem_reg_write_out;
    sc_signal<bool> mem_valid_out;
    sc_signal<sc_uint<32>> mem_instruction_out;

    sc_signal<sc_uint<32>> wb_result_out;
    sc_signal<sc_uint<5>> wb_rd_out;
    sc_signal<bool> wb_reg_write_en;
    sc_signal<bool> wb_valid_out;

    // Register file
    sc_signal<sc_uint<32>> reg_file[32];

    // Module instances
    IFU* ifu;
    Decode* decode;
    Execute* execute;
    Memory* memory;
    Writeback* writeback;

    void update_opcode() {
        opcode.write(decode_instruction_out.read().range(31, 25));
    }

    void update_monitor_outputs() {
        monitor_pc.write(pc_out.read());
        monitor_instruction.write(ifu_instruction_out.read());
        monitor_valid.write(ifu_valid_out.read());
    }

    void reg_file_update() {
        // Initialize registers on reset
        if (reset.read()) {
            reg_file[1].write(FLOAT_5_5);
            reg_file[2].write(FLOAT_2_5);
            reg_file[4].write(FLOAT_10_0);
            reg_file[5].write(FLOAT_3_0);
            reg_file[7].write(FLOAT_4_0);
            reg_file[8].write(FLOAT_2_5);
            reg_file[10].write(FLOAT_15_0);
            reg_file[11].write(FLOAT_3_0);

            for (int i = 16; i <= 19; i++) {
                reg_file[i].write(0);
            }
            cout << "REG @" << sc_time_stamp() << ": Register file initialized" << endl;
        }
        wait();

        while (true) {
            if (wb_reg_write_en.read() && wb_valid_out.read()) {
                if (wb_rd_out.read() < 32) {
                    reg_file[wb_rd_out.read()].write(wb_result_out.read());
                    cout << "REG @" << sc_time_stamp() << ": ";
                    cout << "f" << wb_rd_out.read() << " updated to 0x" << hex << wb_result_out.read() << endl;
                }
            }
            wait();
        }
    }

    SC_CTOR(Top) {
        ifu = new IFU("ifu");
        decode = new Decode("decode");
        execute = new Execute("execute");
        memory = new Memory("memory");
        writeback = new Writeback("writeback");

        // Connect IFU
        ifu->clk(clk);
        ifu->reset(reset);
        ifu->stall(stall);
        ifu->pc_out(pc_out);
        ifu->instruction_out(ifu_instruction_out);
        ifu->valid_out(ifu_valid_out);

        // Connect Decode
        decode->reset(reset);
        decode->stall(stall);
        decode->valid_in(ifu_valid_out);
        decode->instruction_in(ifu_instruction_out);
        for (int i = 0; i < 32; i++) {
            decode->reg_file[i](reg_file[i]);
        }
        decode->op1_out(op1_out);
        decode->op2_out(op2_out);
        decode->rd_out(rd_out);
        decode->reg_write_out(reg_write_out);
        decode->valid_out(decode_valid_out);
        decode->instruction_out(decode_instruction_out);

        // Connect Execute
        execute->clk(clk);
        execute->reset(reset);
        execute->stall(stall);
        execute->valid_in(decode_valid_out);
        execute->op1(op1_out);
        execute->op2(op2_out);
        execute->opcode(opcode);
        execute->rd_in(rd_out);
        execute->reg_write_in(reg_write_out);
        execute->instruction_in(decode_instruction_out);
        execute->result_out(ex_result_out);
        execute->rd_out(ex_rd_out);
        execute->reg_write_out(ex_reg_write_out);
        execute->valid_out(ex_valid_out);
        execute->instruction_out(ex_instruction_out);

        // Connect Memory
        memory->reset(reset);
        memory->stall(stall);
        memory->valid_in(ex_valid_out);
        memory->result_in(ex_result_out);
        memory->rd_in(ex_rd_out);
        memory->reg_write_in(ex_reg_write_out);
        memory->instruction_in(ex_instruction_out);
        memory->result_out(mem_result_out);
        memory->rd_out(mem_rd_out);
        memory->reg_write_out(mem_reg_write_out);
        memory->valid_out(mem_valid_out);
        memory->instruction_out(mem_instruction_out);

        // Connect Writeback
        writeback->reset(reset);
        writeback->stall(stall);
        writeback->valid_in(mem_valid_out);
        writeback->result_in(mem_result_out);
        writeback->rd_in(mem_rd_out);
        writeback->reg_write_in(mem_reg_write_out);
        writeback->instruction_in(mem_instruction_out);
        writeback->result_out(wb_result_out);
        writeback->rd_out(wb_rd_out);
        writeback->reg_write_en(wb_reg_write_en);
        writeback->valid_out(wb_valid_out);

        SC_METHOD(update_opcode);
        sensitive << decode_instruction_out;

        SC_METHOD(update_monitor_outputs);
        sensitive << pc_out << ifu_instruction_out << ifu_valid_out;

        SC_CTHREAD(reg_file_update, clk.pos());
        reset_signal_is(reset, true);
    }

    ~Top() {
        delete ifu;
        delete decode;
        delete execute;
        delete memory;
        delete writeback;
    }
};

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> reset;
    sc_signal<bool> stall;
    sc_signal<sc_uint<32>> monitor_pc;
    sc_signal<sc_uint<32>> monitor_instruction;
    sc_signal<bool> monitor_valid;

    Top top("top");
    top.clk(clk);
    top.reset(reset);
    top.stall(stall);
    top.monitor_pc(monitor_pc);
    top.monitor_instruction(monitor_instruction);
    top.monitor_valid(monitor_valid);

    // Open VCD trace file
    sc_trace_file *wf = sc_create_vcd_trace_file("processor");
    sc_trace(wf, clk, "clk");
    sc_trace(wf, reset, "reset");
    sc_trace(wf, stall, "stall");
    sc_trace(wf, monitor_pc, "monitor_pc");
    sc_trace(wf, monitor_instruction, "monitor_instruction");
    sc_trace(wf, monitor_valid, "monitor_valid");

    // Simulation
    reset.write(true);
    sc_start(15, SC_NS); // Time 15

    reset.write(false);
    sc_start(285, SC_NS); // Run until 300ns total

    // Print final register state
    cout << "\nFinal Register File Contents:" << endl;
    for (int i = 1; i <= 19; i++) {
        if (i <= 11 || (i >=16 && i <=19)) {
            cout << "f" << i << ": 0x" << hex << top.reg_file[i].read() << endl;
        }
    }

    sc_close_vcd_trace_file(wf);
    return 0;
}

