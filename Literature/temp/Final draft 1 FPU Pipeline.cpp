#include <systemc.h>
#include "IEEE754Add.h"
#include "IEEE754Div.h"
#include "IEEE754Mult.h"
#include "IEEE754Sub.h"
#include "mem_wb.h"
#include "execute.h"

// Separate instruction memory module
SC_MODULE(InstructionMemory) {
    sc_in<sc_uint<32>> address;
    sc_out<sc_uint<32>> instruction;
    
    // Instruction memory array
    sc_signal<sc_uint<32>> imem[256];
    
    void process_read() {
        // Read from instruction memory based on address (word-aligned)
        instruction.write(imem[address.read().range(9,2)].read());
    }
    
    // No hardcoded program loading - testbench will control this
    
    SC_CTOR(InstructionMemory) {
        SC_METHOD(process_read);
        sensitive << address;
        for (int i = 0; i < 256; i++) {
            sensitive << imem[i];
        }
    }
};

// Main FP system module
SC_MODULE(FpSystem) {
    // Clock and reset
    sc_in<bool> clk;
    sc_in<bool> reset;

    // Internal signals for pipeline stages
    sc_signal<bool> stall;
    
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

    // Register file - publicly accessible for testbench initialization
    sc_signal<sc_uint<32>> reg_file[32];
    
    // Signal for connecting to instruction memory
    sc_signal<sc_uint<32>> imem_address;
    sc_signal<sc_uint<32>> imem_instruction;

    // Submodules
    InstructionMemory imem;
    Execute execute;
    Memory memory;
    Writeback writeback;

    void update_opcode() {
        opcode.write(decode_instruction_out.read().range(31, 25));
    }

    void ifu_process() {
        // Local state
        sc_uint<32> pc = 0;
        bool terminated = false;
        
        // Reset outputs
        ifu_instruction_out.write(0);
        ifu_valid_out.write(false);
        pc_out.write(0);
        imem_address.write(0);
        
        while (true) {
            if (reset.read()) {
                // Reset state
                pc = 0;
                terminated = false;
                ifu_instruction_out.write(0);
                ifu_valid_out.write(false);
                pc_out.write(0);
                imem_address.write(0);
            } else if (!stall.read() && !terminated) {
                // Get current PC
                sc_uint<32> current_pc = pc;
                
                // Set address for instruction memory
                imem_address.write(current_pc);
                
                // Get instruction from memory module
                sc_uint<32> instruction = imem_instruction.read();
                
                // Update outputs
                ifu_instruction_out.write(instruction);
                ifu_valid_out.write(instruction != 0);
                pc_out.write(current_pc);
                
                // Check for program termination
                if (instruction == 0) {
                    terminated = true;
                    ifu_valid_out.write(false);
                } else {
                    // Increment PC (always by 4 for word alignment)
                    pc = current_pc + 4;
                }
                
                cout << "IFU @" << sc_time_stamp() << ": PC=" << hex << current_pc 
                     << " Instruction=0x" << instruction << endl;
                
                if (terminated && pc_out.read() >= 16) {
                    // Print final register values
                    cout << "\nFinal Register File Contents:" << endl;
                    for (int i = 1; i <= 19; i++) {
                        if (i <= 11 || (i >= 16 && i <= 19)) {
                            cout << "f" << i << ": 0x" << hex << reg_file[i].read() << endl;
                        }
                    }
                    
                    cout << "\n=== Simulation Complete ===\n" << endl;
                    sc_stop();  // End simulation
                }
            }
            
            wait();  // Wait for next clock
        }
    }

    void decode_process() {
        // Reset outputs
        op1_out.write(0);
        op2_out.write(0);
        rd_out.write(0);
        reg_write_out.write(false);
        decode_valid_out.write(false);
        decode_instruction_out.write(0);
        
        while (true) {
            if (reset.read()) {
                // Reset state
                op1_out.write(0);
                op2_out.write(0);
                rd_out.write(0);
                reg_write_out.write(false);
                decode_valid_out.write(false);
                decode_instruction_out.write(0);
            } else if (!stall.read()) {
                // Forward valid signal and instruction
                decode_valid_out.write(ifu_valid_out.read());
                decode_instruction_out.write(ifu_instruction_out.read());
                
                // Only decode if instruction is valid
                if (ifu_valid_out.read() && ifu_instruction_out.read() != 0) {
                    // Extract register indices
                    sc_uint<5> rs1 = (ifu_instruction_out.read() >> 15) & 0x1F;
                    sc_uint<5> rs2 = (ifu_instruction_out.read() >> 20) & 0x1F;
                    sc_uint<5> rd = (ifu_instruction_out.read() >> 7) & 0x1F;
                    
                    // Get register values
                    op1_out.write(reg_file[rs1.to_uint()].read());
                    op2_out.write(reg_file[rs2.to_uint()].read());
                    rd_out.write(rd);
                    reg_write_out.write(true);
                    
                    cout << "DEC @" << sc_time_stamp() << ": ";
                    cout << "rs1=f" << rs1 << " (0x" << hex << reg_file[rs1.to_uint()].read() << ") ";
                    cout << "rs2=f" << rs2 << " (0x" << hex << reg_file[rs2.to_uint()].read() << ") ";
                    cout << "rd=f" << rd << endl;
                } else {
                    // Invalid instruction
                    op1_out.write(0);
                    op2_out.write(0);
                    rd_out.write(0);
                    reg_write_out.write(false);
                }
            }
            
            wait();  // Wait for next clock
        }
    }

    void reg_file_update() {
        while (true) {
            if (reset.read()) {
                // No need to reset register file here
            } else if (wb_reg_write_en.read() && wb_valid_out.read()) {
                unsigned rd_index = wb_rd_out.read().to_uint();
                if (rd_index < 32) {
                    reg_file[rd_index].write(wb_result_out.read());
                    cout << "REG @" << sc_time_stamp() << ": ";
                    cout << "f" << rd_index << " updated to 0x" << hex << wb_result_out.read() << endl;
                }
            }
            wait();  // Wait for next clock
        }
    }

    SC_CTOR(FpSystem) : 
        imem("instruction_memory"),
        execute("execute"),
        memory("memory"),
        writeback("writeback")
    {
        // Initialize stall
        stall.write(false);
        
        // Connect instruction memory
        imem.address(imem_address);
        imem.instruction(imem_instruction);
        
        // Connect Execute
        execute.clk(clk);
        execute.reset(reset);
        execute.stall(stall);
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

        // Connect Memory
        memory.reset(reset);
        memory.stall(stall);
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

        // Connect Writeback
        writeback.reset(reset);
        writeback.stall(stall);
        writeback.valid_in(mem_valid_out);
        writeback.result_in(mem_result_out);
        writeback.rd_in(mem_rd_out);
        writeback.reg_write_in(mem_reg_write_out);
        writeback.instruction_in(mem_instruction_out);
        writeback.result_out(wb_result_out);
        writeback.rd_out(wb_rd_out);
        writeback.reg_write_en(wb_reg_write_en);
        writeback.valid_out(wb_valid_out);

        // Register processes
        SC_METHOD(update_opcode);
        sensitive << decode_instruction_out;
        
        SC_CTHREAD(ifu_process, clk.pos());
        reset_signal_is(reset, true);
        
        SC_CTHREAD(decode_process, clk.pos());
        reset_signal_is(reset, true);
        
        SC_CTHREAD(reg_file_update, clk.pos());
        reset_signal_is(reset, true);
    }
};

// Top-level function
int sc_main(int argc, char* argv[]) {
    // System clock and reset
    sc_clock clock("clk", 10, SC_NS);
    sc_signal<bool> reset;
    
    // Create the FP system module
    FpSystem system("system");
    
    // Connect clock and reset
    system.clk(clock);
    system.reset(reset);
    
    // Create trace file
    sc_trace_file *wf = sc_create_vcd_trace_file("fp_system");
    sc_trace(wf, clock, "clk");
    sc_trace(wf, reset, "reset");
    sc_trace(wf, system.pc_out, "pc_out");
    sc_trace(wf, system.ifu_instruction_out, "instruction");
    sc_trace(wf, system.ifu_valid_out, "valid");
    
    // Simple test sequence
    cout << "\nStarting simulation..." << endl;
    
    // Apply reset
    reset.write(true);
    sc_start(15, SC_NS);
    
    // Remove reset and run simulation
    reset.write(false);
    sc_start(1000, SC_NS);  // Run for a while or until sc_stop() is called
    
    // Close trace file
    sc_close_vcd_trace_file(wf);
    
    return 0;
}
