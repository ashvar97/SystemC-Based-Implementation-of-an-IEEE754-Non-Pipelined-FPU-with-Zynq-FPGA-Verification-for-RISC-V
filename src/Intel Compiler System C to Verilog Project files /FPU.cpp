#include <systemc.h>
#include "IEEE754Add.h"
#include "IEEE754Div.h"
#include "IEEE754Mult.h"
#include "IEEE754Sub.h"
#include "mem_wb.h"
#include "execute.h"
#include "imem.h"

SC_MODULE(FPPipelinedProcessor) {
    sc_in<bool> clk;
    sc_in<bool> reset;
    sc_in<bool> stall;
    sc_out<bool> monitor_valid;
    sc_out<sc_uint<8>> monitor_pc;

    sc_signal<bool> internal_stall;
    
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

    // Fixed: Changed from ac_uint to sc_uint
    sc_signal<sc_uint<32>> mem_result_out;
    sc_signal<sc_uint<5>> mem_rd_out;
    sc_signal<bool> mem_reg_write_out;
    sc_signal<bool> mem_valid_out;
    sc_signal<sc_uint<32>> mem_instruction_out;

    sc_signal<sc_uint<32>> wb_result_out;
    sc_signal<sc_uint<5>> wb_rd_out;
    sc_signal<bool> wb_reg_write_en;
    sc_signal<bool> wb_valid_out;

    sc_signal<sc_uint<32>> reg_file[32];
    
    sc_signal<sc_uint<32>> imem_address;
    sc_signal<sc_uint<32>> imem_instruction;

    InstructionMemory imem;
    Execute execute;
    Memory memory;
    Writeback writeback;

    void update_opcode() {
        opcode.write(decode_instruction_out.read().range(31, 25));
    }

    void update_stall() {
        internal_stall.write(stall.read());
    }

    void ifu_process() {
        sc_uint<32> pc = 0;
        bool terminated = false;
        
        ifu_instruction_out.write(0);
        ifu_valid_out.write(false);
        pc_out.write(0);
        imem_address.write(0);
        
        while (true) {
            if (reset.read()) {
                pc = 0;
                terminated = false;
                ifu_instruction_out.write(0);
                ifu_valid_out.write(false);
                pc_out.write(0);
                imem_address.write(0);
            } else if (!internal_stall.read() && !terminated) {
                sc_uint<32> current_pc = pc;
                
                imem_address.write(current_pc);
                
                sc_uint<32> instruction = imem_instruction.read();
                
                ifu_instruction_out.write(instruction);
                ifu_valid_out.write(instruction != 0);
                pc_out.write(current_pc);
                
                if (instruction == 0) {
                    terminated = true;
                    ifu_valid_out.write(false);
                } else {
                    pc = current_pc + 4;
                }
                
                cout << "IFU @" << sc_time_stamp() << ": PC=" << hex << current_pc 
                     << " Instruction=0x" << instruction << endl;
                
                if (terminated && pc_out.read() >= 16) {
                    cout << "\nFinal Register File Contents:" << endl;
                    for (int i = 1; i <= 19; i++) {
                        if (i <= 11 || (i >= 16 && i <= 19)) {
                            cout << "f" << i << ": 0x" << hex << reg_file[i].read() << endl;
                        }
                    }
                    
                    cout << "\n=== Simulation Complete ===\n" << endl;
                    sc_stop();
                }
            }
            
            wait();
        }
    }

    void decode_process() {
        op1_out.write(0);
        op2_out.write(0);
        rd_out.write(0);
        reg_write_out.write(false);
        decode_valid_out.write(false);
        decode_instruction_out.write(0);
        
        while (true) {
            if (reset.read()) {
                op1_out.write(0);
                op2_out.write(0);
                rd_out.write(0);
                reg_write_out.write(false);
                decode_valid_out.write(false);
                decode_instruction_out.write(0);
            } else if (!internal_stall.read()) {
                decode_valid_out.write(ifu_valid_out.read());
                decode_instruction_out.write(ifu_instruction_out.read());
                
                if (ifu_valid_out.read() && ifu_instruction_out.read() != 0) {
                    sc_uint<5> rs1 = (ifu_instruction_out.read() >> 15) & 0x1F;
                    sc_uint<5> rs2 = (ifu_instruction_out.read() >> 20) & 0x1F;
                    sc_uint<5> rd = (ifu_instruction_out.read() >> 7) & 0x1F;
                    
                    op1_out.write(reg_file[rs1.to_uint()].read());
                    op2_out.write(reg_file[rs2.to_uint()].read());
                    rd_out.write(rd);
                    reg_write_out.write(true);
                    
                    cout << "DEC @" << sc_time_stamp() << ": ";
                    cout << "rs1=f" << rs1 << " (0x" << hex << reg_file[rs1.to_uint()].read() << ") ";
                    cout << "rs2=f" << rs2 << " (0x" << hex << reg_file[rs2.to_uint()].read() << ") ";
                    cout << "rd=f" << rd << endl;
                } else {
                    op1_out.write(0);
                    op2_out.write(0);
                    rd_out.write(0);
                    reg_write_out.write(false);
                }
            }
            
            wait();
        }
    }

    void reg_file_update() {
        while (true) {
            if (reset.read()) {
            } else if (wb_reg_write_en.read() && wb_valid_out.read()) {
                unsigned rd_index = wb_rd_out.read().to_uint();
                if (rd_index < 32) {
                    reg_file[rd_index].write(wb_result_out.read());
                    cout << "REG @" << sc_time_stamp() << ": ";
                    cout << "f" << rd_index << " updated to 0x" << hex << wb_result_out.read() << endl;
                }
            }
            wait();
        }
    }
    
    void update_monitor() {
        monitor_valid.write(wb_valid_out.read());
        monitor_pc.write(pc_out.read().range(7, 0));
    }

    SC_CTOR(FPPipelinedProcessor) : 
        imem("instruction_memory"),
        execute("execute"),
        memory("memory"),
        writeback("writeback")
    {
        SC_METHOD(update_stall);
        sensitive << stall;
        
        imem.address(imem_address);
        imem.instruction(imem_instruction);
        
        execute.clk(clk);
        execute.reset(reset);
        execute.stall(internal_stall);
        execute.valid_in(decode_valid_out);
        execute.op1(op1_out);
        execute.op2(op2_out);
        execute.opcode(opcode);
        execute.rd_in(rd_out);
        execute.reg_write_in(reg_write_out);
        execute.instruction_in(decode_instruction_out);
        execute.result_out(ex_result_out);
        execute.rd_out(ex_rd_out);
        execute.reg_write_out(ex_reg_write_out);
        execute.valid_out(ex_valid_out);
        execute.instruction_out(ex_instruction_out);

        memory.reset(reset);
        memory.stall(internal_stall);
        memory.valid_in(ex_valid_out);
        memory.result_in(ex_result_out);
        memory.rd_in(ex_rd_out);
        memory.reg_write_in(ex_reg_write_out);
        memory.instruction_in(ex_instruction_out);
        memory.result_out(mem_result_out);
        memory.rd_out(mem_rd_out);
        memory.reg_write_out(mem_reg_write_out);
        memory.valid_out(mem_valid_out);
        memory.instruction_out(mem_instruction_out);

        writeback.reset(reset);
        writeback.stall(internal_stall);
        writeback.valid_in(mem_valid_out);
        writeback.result_in(mem_result_out);
        writeback.rd_in(mem_rd_out);
        writeback.reg_write_in(mem_reg_write_out);
        writeback.instruction_in(mem_instruction_out);
        writeback.result_out(wb_result_out);
        writeback.rd_out(wb_rd_out);
        writeback.reg_write_en(wb_reg_write_en);
        writeback.valid_out(wb_valid_out);

        SC_METHOD(update_opcode);
        sensitive << decode_instruction_out;
        
        SC_METHOD(update_monitor);
        sensitive << wb_valid_out << pc_out;
        
        SC_CTHREAD(ifu_process, clk.pos());
        reset_signal_is(reset, true);
        
        SC_CTHREAD(decode_process, clk.pos());
        reset_signal_is(reset, true);
        
        SC_CTHREAD(reg_file_update, clk.pos());
        reset_signal_is(reset, true);
    }
};


uint32_t floatToHex(float value) {
    uint32_t result;
    memcpy(&result, &value, sizeof(float));
    return result;
}

float hexToFloat(uint32_t value) {
    float result;
    memcpy(&result, &value, sizeof(uint32_t));
    return result;
}

int sc_main(int argc, char* argv[]) {
    // Clock and control signals
    sc_clock clock("clk", 10, SC_NS);
    sc_signal<bool> reset;
    sc_signal<bool> stall_signal;
    sc_signal<bool> monitor_valid;
    sc_signal<sc_uint<8>> monitor_pc;
    
    // Instantiate the processor
    FPPipelinedProcessor system("system");
    
    // Connect signals
    system.clk(clock);
    system.reset(reset);
    system.stall(stall_signal);
    system.monitor_valid(monitor_valid);
    system.monitor_pc(monitor_pc);
    
    // Simple initialization
    reset.write(true);
    stall_signal.write(false);
    
    // Reset phase
    sc_start(20, SC_NS);
    reset.write(false);
    
    // Run simulation
    sc_start(1000, SC_NS);
    
    return 0;
}
