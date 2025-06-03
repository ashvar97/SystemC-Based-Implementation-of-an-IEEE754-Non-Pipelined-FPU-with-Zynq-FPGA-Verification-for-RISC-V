#ifndef PIPELINE_H
#define PIPELINE_H

#include <systemc.h>
#include <iostream>
#include <iomanip>

// Union for float-uint32 conversion
union float_uint32 {
    float f;
    uint32_t u;
};

//-------------------------------------------
// Instruction Fetch Unit (IFU)
//-------------------------------------------
SC_MODULE(IFU) {
    sc_in<sc_uint<32>> pc_in;
    sc_out<sc_uint<32>> pc_out;
    sc_out<sc_uint<32>> instruction_out;
    
    sc_uint<32> pc;
    sc_uint<32> imem[1024];
    
    void fetch_instruction() {
        while(true) {
            wait(10, SC_NS);
            sc_uint<32> insn = imem[pc >> 2];
            
            if(insn == 0x00000000) {
                std::cout << "Program terminated" << std::endl;
                sc_stop();
                break;
            }
            
            instruction_out.write(insn);
            std::cout << "[IF] Fetched: 0x" << std::hex << insn 
                     << " at PC: 0x" << pc << std::dec << std::endl;
            pc += 4;
            pc_out.write(pc);
        }
    }
    
    SC_CTOR(IFU) {
        SC_THREAD(fetch_instruction);
    }
};

//-------------------------------------------
// Execute Stage
//-------------------------------------------
SC_MODULE(Execute) {
    sc_in<sc_uint<32>> op1, op2;
    sc_in<sc_uint<3>> opcode;
    sc_in<sc_uint<5>> rd_in;
    sc_in<bool> reg_write_in;
    
    sc_out<sc_uint<32>> result_out;
    sc_out<sc_uint<5>> rd_out;
    sc_out<bool> reg_write_out;
    
    void execute_operation() {
        float_uint32 a, b, r;
        a.u = op1.read();
        b.u = op2.read();

        switch(opcode.read()) {
            case 0: r.f = a.f + b.f; break;  // fadd.s
            case 1: r.f = a.f - b.f; break;  // fsub.s
            case 2: r.f = a.f * b.f; break;  // fmul.s
            case 3: r.f = a.f / b.f; break;  // fdiv.s
            default: r.f = 0.0f;
        }

        result_out.write(r.u);
        rd_out.write(rd_in.read());
        reg_write_out.write(reg_write_in.read());
        
        std::cout << "[EX] Executed: result=" << r.f 
                 << " for rd=f" << rd_in.read() << std::endl;
    }

    SC_CTOR(Execute) {
        SC_METHOD(execute_operation);
        sensitive << op1 << op2 << opcode << rd_in << reg_write_in;
    }
};

//-------------------------------------------
// Memory Stage
//-------------------------------------------
SC_MODULE(Memory) {
    sc_in<sc_uint<32>> result_in;
    sc_in<sc_uint<5>> rd_in;
    sc_in<bool> reg_write_in;
    
    sc_out<sc_uint<32>> result_out;
    sc_out<sc_uint<5>> rd_out;
    sc_out<bool> reg_write_out;
    
    void memory_access() {
        result_out.write(result_in.read());
        rd_out.write(rd_in.read());
        reg_write_out.write(reg_write_in.read());
        
        std::cout << "[MEM] Passing through result for f" 
                 << rd_in.read() << std::endl;
    }

    SC_CTOR(Memory) {
        SC_METHOD(memory_access);
        sensitive << result_in << rd_in << reg_write_in;
    }
};

//-------------------------------------------
// Writeback Stage
//-------------------------------------------
SC_MODULE(Writeback) {
    sc_in<sc_uint<32>> result_in;
    sc_in<sc_uint<5>> rd_in;
    sc_in<bool> reg_write_in;
    
    sc_out<sc_uint<32>> result_out;
    sc_out<sc_uint<5>> rd_out;
    sc_out<bool> reg_write_en;
    
    void write_back() {
        result_out.write(result_in.read());
        rd_out.write(rd_in.read());
        reg_write_en.write(reg_write_in.read());
        
        if(reg_write_in.read()) {
            std::cout << "[WB] Writing to f" << rd_in.read() << std::endl;
        }
    }

    SC_CTOR(Writeback) {
        SC_METHOD(write_back);
        sensitive << result_in << rd_in << reg_write_in;
    }
};

//-------------------------------------------
// Complete Testbench
//-------------------------------------------
SC_MODULE(Testbench) {
    // Pipeline registers
    sc_signal<sc_uint<32>> if_id_insn, id_ex_op1, id_ex_op2;
    sc_signal<sc_uint<32>> ex_mem_result, mem_wb_result, wb_result;
    sc_signal<sc_uint<3>> id_ex_opcode;
    sc_signal<sc_uint<5>> id_ex_rd, ex_mem_rd, mem_wb_rd, wb_rd;
    sc_signal<bool> id_ex_reg_write, ex_mem_reg_write, mem_wb_reg_write, wb_reg_write_en;
    
    // IFU connections
    sc_signal<sc_uint<32>> pc_in, pc_out;
    
    // Modules
    IFU *ifu;
    Execute *execute;
    Memory *memory;
    Writeback *writeback;
    
    // Register file
    float reg_file[32];
    
    void initialize() {
        // Initialize program
        ifu->imem[0] = 0x00208053;  // fadd.s f0, f1, f2
        ifu->imem[1] = 0x08520253;   // fsub.s f3, f4, f5
        ifu->imem[2] = 0x108383D3;   // fmul.s f6, f7, f8
        ifu->imem[3] = 0x18B504D3;   // fdiv.s f9, f10, f11
        ifu->imem[4] = 0x00000000;   // Terminate
        
        // Initialize register file
        for(int i=0; i<32; i++) reg_file[i] = 0.0f;
        reg_file[1] = 5.5f;    // f1
        reg_file[2] = 2.5f;    // f2
        reg_file[4] = 10.0f;   // f4
        reg_file[5] = 3.0f;    // f5
        reg_file[7] = 4.0f;    // f7
        reg_file[8] = 2.5f;    // f8
        reg_file[10] = 15.0f;  // f10
        reg_file[11] = 3.0f;   // f11
        
        std::cout << "Initialized registers:" << std::endl;
        for(int i=0; i<32; i++) {
            if(reg_file[i] != 0.0f) {
                std::cout << "f" << i << " = " << reg_file[i] << std::endl;
            }
        }
    }
    
    void decode() {
        sc_uint<32> insn = if_id_insn.read();
        sc_uint<7> opcode = insn & 0x7F;
        sc_uint<3> funct3 = (insn >> 12) & 0x7;
        sc_uint<7> funct7 = (insn >> 25) & 0x7F;
        
        if(opcode == 0x53) {  // FP opcode
            sc_uint<5> rs1 = (insn >> 15) & 0x1F;
            sc_uint<5> rs2 = (insn >> 20) & 0x1F;
            sc_uint<5> rd = (insn >> 7) & 0x1F;
            
            float_uint32 op1, op2;
            op1.f = reg_file[rs1];
            op2.f = reg_file[rs2];
            
            id_ex_op1.write(op1.u);
            id_ex_op2.write(op2.u);
            id_ex_rd.write(rd);
            id_ex_reg_write.write(true);
            
            if(funct3 == 0x0) {
                switch(funct7) {
                    case 0x00: id_ex_opcode.write(0); break; // fadd.s
                    case 0x04: id_ex_opcode.write(1); break; // fsub.s
                    case 0x08: id_ex_opcode.write(2); break; // fmul.s
                    case 0x0C: id_ex_opcode.write(3); break; // fdiv.s
                    default: id_ex_opcode.write(0);
                }
            }
            
            std::cout << "[ID] Decoded: rs1=f" << rs1 << " rs2=f" << rs2 
                     << " rd=f" << rd << std::endl;
        }
    }
    
    void register_write() {
        if(wb_reg_write_en.read()) {
            float_uint32 result;
            result.u = wb_result.read();
            reg_file[wb_rd.read()] = result.f;
            std::cout << "[REG] Wrote " << result.f << " to f" 
                     << wb_rd.read() << std::endl;
        }
    }
    
    void run_simulation() {
        wait(100, SC_NS);
        
        std::cout << "\nFinal register values:" << std::endl;
        for(int i=0; i<32; i++) {
            if(reg_file[i] != 0.0f) {
                std::cout << "f" << i << " = " << reg_file[i] << std::endl;
            }
        }
        
        sc_stop();
    }
    
    SC_CTOR(Testbench) {
        // Create pipeline stages
        ifu = new IFU("IFU");
        execute = new Execute("Execute");
        memory = new Memory("Memory");
        writeback = new Writeback("Writeback");
        
        // Connect IFU
        ifu->pc_in(pc_in);
        ifu->pc_out(pc_out);
        ifu->instruction_out(if_id_insn);
        
        // Connect Execute stage
        execute->op1(id_ex_op1);
        execute->op2(id_ex_op2);
        execute->opcode(id_ex_opcode);
        execute->rd_in(id_ex_rd);
        execute->reg_write_in(id_ex_reg_write);
        
        // Connect Memory stage
        execute->result_out(ex_mem_result);
        execute->rd_out(ex_mem_rd);
        execute->reg_write_out(ex_mem_reg_write);
        
        memory->result_in(ex_mem_result);
        memory->rd_in(ex_mem_rd);
        memory->reg_write_in(ex_mem_reg_write);
        
        // Connect Writeback stage
        memory->result_out(mem_wb_result);
        memory->rd_out(mem_wb_rd);
        memory->reg_write_out(mem_wb_reg_write);
        
        writeback->result_in(mem_wb_result);
        writeback->rd_in(mem_wb_rd);
        writeback->reg_write_in(mem_wb_reg_write);
        
        // Connect Writeback outputs
        writeback->result_out(wb_result);
        writeback->rd_out(wb_rd);
        writeback->reg_write_en(wb_reg_write_en);
        
        // Initialize and run
        SC_THREAD(initialize);
        SC_THREAD(run_simulation);
        
        // Register processes
        SC_METHOD(decode);
        sensitive << if_id_insn;
        
        SC_METHOD(register_write);
        sensitive << wb_result << wb_rd << wb_reg_write_en;
    }
};

#endif // PIPELINE_H
int sc_main(int argc, char* argv[]) {
    std::cout << "Starting 5-Stage FPU Pipeline Simulation" << std::endl;
    std::cout << "=======================================" << std::endl;
    
    Testbench tb("Testbench");
    sc_start();
    
    std::cout << "Simulation completed" << std::endl;
    return 0;
}
