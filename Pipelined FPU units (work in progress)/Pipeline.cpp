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

    // Memory stage signals
    sc_signal<sc_uint<32>> mem_result_out;
    sc_signal<sc_uint<5>> mem_rd_out;
    sc_signal<bool> mem_reg_write_out;
    sc_signal<bool> mem_valid_out;
    sc_signal<sc_uint<32>> mem_instruction_out;

    // Writeback stage signals
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
        sc_uint<32> instruction = decode_instruction_out.read();
        sc_uint<7> base_opcode = instruction.range(6, 0);
        
        if (base_opcode == 0x53 && decode_valid_out.read()) { // FP operation
            // CRITICAL FIX: For FP operations, use funct7 field (bits 31-25) 
            sc_uint<7> funct7 = instruction.range(31, 25);
            opcode.write(funct7);
            
            cout << "OPCODE @" << sc_time_stamp() << ": instruction=0x" << hex 
                 << instruction << " -> opcode=0x" << funct7 
                 << " (base=0x" << base_opcode << ")" << dec << endl;
        } else {
            // For other operations, use base opcode
            opcode.write(base_opcode);
        }
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
                
                cout << "IFU @" << sc_time_stamp() << ": PC=0x" << hex << current_pc 
                     << " Instruction=0x" << instruction << dec << endl;
                
                // Extended termination check for longer pipeline (division is 27 cycles)
                if (terminated && pc_out.read() >= 40) {
                    wait(100, SC_NS); // Extended wait for division pipeline to drain
                    
                    cout << "\nFinal Register File Contents:" << endl;
                    for (int i = 1; i <= 16; i++) {
                        if (reg_file[i].read() != 0) {
                            float val = hexToFloat(reg_file[i].read());
                            cout << "f" << i << ": 0x" << hex << reg_file[i].read() 
                                 << dec << " (" << val << ")" << endl;
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
                    sc_uint<32> instruction = ifu_instruction_out.read();
                    sc_uint<5> rs1 = instruction.range(19, 15);
                    sc_uint<5> rs2 = instruction.range(24, 20);
                    sc_uint<5> rd = instruction.range(11, 7);
                    
                    op1_out.write(reg_file[rs1.to_uint()].read());
                    op2_out.write(reg_file[rs2.to_uint()].read());
                    rd_out.write(rd);
                    reg_write_out.write(true);
                    
                    cout << "DEC @" << sc_time_stamp() << ": ";
                    cout << "rs1=f" << rs1 << " (0x" << hex << reg_file[rs1.to_uint()].read() << ") ";
                    cout << "rs2=f" << rs2 << " (0x" << hex << reg_file[rs2.to_uint()].read() << ") ";
                    cout << "rd=f" << rd << dec << endl;
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
                // Initialize register file with test values including division testing
                reg_file[0].write(0x00000000);  // f0 = 0.0 (always zero)
                reg_file[1].write(0x40490FDB);  // f1 = 3.14159 (Pi)
                reg_file[2].write(0x402DF854);  // f2 = 2.71828 (e)
                reg_file[3].write(0x00000000);  // f3 = result register
                reg_file[4].write(0x40000000);  // f4 = 2.0
                reg_file[5].write(0x40400000);  // f5 = 3.0
                reg_file[6].write(0x00000000);  // f6 = result register
                reg_file[7].write(0xBFC00000);  // f7 = -1.5
                reg_file[8].write(0x40800000);  // f8 = 4.0
                reg_file[9].write(0x00000000);  // f9 = result register
                reg_file[10].write(0x3F000000); // f10 = 0.5
                reg_file[11].write(0x3E800000); // f11 = 0.25
                reg_file[12].write(0x41000000); // f12 = 8.0 (for division tests)
                reg_file[13].write(0x40A00000); // f13 = 5.0 (for division tests)
                
                // Initialize remaining registers to zero
                for (int i = 14; i < 32; i++) {
                    reg_file[i].write(0x00000000);
                }
                
                cout << "REG @" << sc_time_stamp() << ": Register file initialized with test values" << endl;
                cout << "  f1 = Pi (3.14159), f2 = e (2.71828)" << endl;
                cout << "  f4 = 2.0, f5 = 3.0, f7 = -1.5, f8 = 4.0" << endl;
                cout << "  f10 = 0.5, f11 = 0.25, f12 = 8.0, f13 = 5.0" << endl;
            } else if (wb_reg_write_en.read() && wb_valid_out.read()) {
                unsigned rd_index = wb_rd_out.read().to_uint();
                if (rd_index < 32 && rd_index != 0) { // Don't write to f0
                    reg_file[rd_index].write(wb_result_out.read());
                    float val = hexToFloat(wb_result_out.read());
                    cout << "REG @" << sc_time_stamp() << ": ";
                    cout << "f" << rd_index << " updated to 0x" << hex << wb_result_out.read() 
                         << " (" << dec << val << ")" << endl;
                }
            }
            wait();
        }
    }
    
    void update_monitor() {
        monitor_valid.write(wb_valid_out.read());
        monitor_pc.write(pc_out.read().range(7, 0));
    }

    // Helper function for float conversion
    float hexToFloat(uint32_t hex) {
        float result;
        memcpy(&result, &hex, sizeof(float));
        return result;
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
        
        // Connect Execute stage
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

        // Memory stage (now clocked)
        memory.clk(clk);
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

        // Writeback stage (now clocked)
        writeback.clk(clk);
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
        sensitive << decode_instruction_out << decode_valid_out;
        
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
    sc_clock clock("clk", 10, SC_NS);
    sc_signal<bool> reset;
    sc_signal<bool> stall_signal;
    sc_signal<bool> monitor_valid;
    sc_signal<sc_uint<8>> monitor_pc;
    
    FPPipelinedProcessor system("system");
    
    system.clk(clock);
    system.reset(reset);
    system.stall(stall_signal);
    system.monitor_valid(monitor_valid);
    system.monitor_pc(monitor_pc);
    
    sc_trace_file *wf = sc_create_vcd_trace_file("fp_system");
    sc_trace(wf, clock, "clk");
    sc_trace(wf, reset, "reset");
    sc_trace(wf, stall_signal, "stall");
    sc_trace(wf, system.pc_out, "pc_out");
    sc_trace(wf, system.ifu_instruction_out, "instruction");
    sc_trace(wf, system.ifu_valid_out, "valid");
    sc_trace(wf, monitor_valid, "monitor_valid");
    sc_trace(wf, monitor_pc, "monitor_pc");
    
    auto createFPInstruction = [](uint8_t funct7, uint8_t rs2, uint8_t rs1, uint8_t rd) -> uint32_t {
        uint32_t instruction = 0;
        instruction |= (funct7 & 0x7F) << 25;
        instruction |= (rs2 & 0x1F) << 20;
        instruction |= (rs1 & 0x1F) << 15;
        instruction |= 0 << 12;
        instruction |= (rd & 0x1F) << 7;
        instruction |= 0x53;
        return instruction;
    };
    
    cout << "\n================ Floating-Point Processor Test ================\n" << endl;
    cout << "Initializing test sequence..." << endl;
    
    stall_signal.write(true);
    reset.write(true);
    sc_start(15, SC_NS);
    
    reset.write(false);
    stall_signal.write(true);
    sc_start(5, SC_NS);
    
    cout << "Setting initial register values..." << endl;
    
    struct RegInit {
        float value;
        uint8_t reg_num;
        const char* description;
    };
    
    RegInit reg_init[] = {
        {3.14159f, 1, "Pi"},
        {2.71828f, 2, "e (Euler's number)"},
        {1.0f, 7, "One"},
        {0.0f, 8, "Zero"},
        {1.0e30f, 10, "Very large number"},
        {1.0e-30f, 11, "Very small number"}
    };
    
    struct SpecialRegInit {
        uint32_t value;
        uint8_t reg_num;
        const char* description;
    };
    
    SpecialRegInit special_reg_init[] = {
        {0x7f800000, 14, "Positive infinity"},
        {0x7fc00000, 15, "NaN (Not a Number)"}
    };
    
    for (const auto& reg : reg_init) {
        system.wb_result_out.write(floatToHex(reg.value));
        system.wb_rd_out.write(reg.reg_num);
        system.wb_reg_write_en.write(true);
        system.wb_valid_out.write(true);
        sc_start(10, SC_NS);
        cout << "  Initialized r" << (int)reg.reg_num << " with " << reg.description 
             << " (" << reg.value << ", 0x" << std::hex << floatToHex(reg.value) 
             << std::dec << ")" << endl;
    }
    
    for (const auto& reg : special_reg_init) {
        system.wb_result_out.write(reg.value);
        system.wb_rd_out.write(reg.reg_num);
        system.wb_reg_write_en.write(true);
        system.wb_valid_out.write(true);
        sc_start(10, SC_NS);
        cout << "  Initialized r" << (int)reg.reg_num << " with " << reg.description 
             << " (0x" << std::hex << reg.value << std::dec << ")" << endl;
    }
    
    system.wb_valid_out.write(false);
    system.wb_reg_write_en.write(false);
    sc_start(10, SC_NS);
    
    cout << "\nLoading test program into instruction memory..." << endl;
    
    struct TestCase {
        uint8_t funct7;
        uint8_t rs2;
        uint8_t rs1;
        uint8_t rd;
        const char* description;
    };
    
    TestCase test_program[] = {
        {0, 2, 1, 3, "fadd.s r3, r1, r2 (Pi + e)"},
        {4, 2, 1, 4, "fsub.s r4, r1, r2 (Pi - e)"},
        {8, 2, 1, 5, "fmul.s r5, r1, r2 (Pi * e)"},
        {12, 2, 1, 6, "fdiv.s r6, r1, r2 (Pi / e)"},
        
        {12, 8, 7, 9, "fdiv.s r9, r7, r8 (1.0 / 0.0 - Division by zero)"},
        {8, 11, 10, 12, "fmul.s r12, r10, r11 (Very large * Very small)"},
        {0, 10, 10, 13, "fadd.s r13, r10, r10 (Very large + Very large)"},
        
        {8, 7, 1, 16, "fmul.s r16, r1, r7 (Pi * 1.0)"},
        {0, 1, 15, 17, "fadd.s r17, r15, r1 (NaN + Pi)"},
        {12, 1, 1, 18, "fdiv.s r18, r1, r1 (Pi / Pi)"},
        {4, 8, 8, 19, "fsub.s r19, r8, r8 (0.0 - 0.0)"},
        {0, 14, 7, 20, "fadd.s r20, r7, r14 (1.0 + infinity)"}
    };
    
    for (size_t i = 0; i < sizeof(test_program) / sizeof(TestCase); i++) {
        const auto& test = test_program[i];
        uint32_t addr = i * 4;
        uint32_t instr = createFPInstruction(test.funct7, test.rs2, test.rs1, test.rd);
        
        system.imem_address.write(addr);
        system.imem_instruction.write(instr);
        sc_start(5, SC_NS);
        
        cout << "  0x" << std::hex << std::setw(8) << std::setfill('0') << instr 
             << std::dec << " @ 0x" << std::hex << addr << std::dec 
             << ": " << test.description << endl;
    }
    
    system.imem_address.write(sizeof(test_program) / sizeof(TestCase) * 4);
    system.imem_instruction.write(0);
    sc_start(5, SC_NS);
    
    cout << "\nStarting simulation..." << endl;
    stall_signal.write(false);
    sc_start(1000, SC_NS);
    
    cout << "\n================ Expected Results ================\n";
    
    cout << "\n---- Basic Operations ----\n";
    cout << "r3 (Pi + e):      Expected " << (3.14159f + 2.71828f) 
         << " (0x" << std::hex << floatToHex(3.14159f + 2.71828f) << std::dec << ")" << endl;
    cout << "r4 (Pi - e):      Expected " << (3.14159f - 2.71828f)
         << " (0x" << std::hex << floatToHex(3.14159f - 2.71828f) << std::dec << ")" << endl;
    cout << "r5 (Pi * e):      Expected " << (3.14159f * 2.71828f)
         << " (0x" << std::hex << floatToHex(3.14159f * 2.71828f) << std::dec << ")" << endl;
    cout << "r6 (Pi / e):      Expected " << (3.14159f / 2.71828f)
         << " (0x" << std::hex << floatToHex(3.14159f / 2.71828f) << std::dec << ")" << endl;
    
    cout << "\n---- Special Cases ----\n";
    cout << "r9 (1.0 / 0.0):   Expected Infinity (0x7f800000)" << endl;
    cout << "r12 (large * small): Expected value close to 1.0" << endl;
    cout << "r13 (large + large): Expected very large value or Infinity" << endl;
    
    cout << "\n---- Additional Tests ----\n";
    cout << "r16 (Pi * 1.0):   Expected Pi (3.14159, 0x" << std::hex << floatToHex(3.14159f) << std::dec << ")" << endl;
    cout << "r17 (NaN + Pi):   Expected NaN (0x7fc00000 or similar)" << endl;
    cout << "r18 (Pi / Pi):    Expected 1.0 (0x3f800000)" << endl;
    cout << "r19 (0.0 - 0.0):  Expected 0.0 (0x00000000)" << endl;
    cout << "r20 (1.0 + inf):  Expected Infinity (0x7f800000)" << endl;
    
    sc_close_vcd_trace_file(wf);
    
    cout << "\n================ Simulation Complete ================\n";
    cout << "VCD trace file 'fp_system.vcd' generated for waveform analysis." << endl;
    
    return 0;
}
