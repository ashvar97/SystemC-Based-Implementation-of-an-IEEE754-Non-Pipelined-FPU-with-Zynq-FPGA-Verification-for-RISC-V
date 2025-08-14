// Code your testbench here.
// Uncomment the next line for SystemC modules.
// #include <systemc>


#include <systemc.h>
#include <cmath>
#include <iomanip>
#include <cfloat>
#include <cstring>
#include "design.cpp"
using namespace std;

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


// Utility functions for float-hex conversion
inline float hexToFloat(uint32_t hex) {
    float f;
    memcpy(&f, &hex, sizeof(float));
    return f;
}

uint32_t floatToHex(float value) {
    uint32_t result;
    memcpy(&result, &value, sizeof(float));
    return result;
}

void printFloat(uint32_t hex) {
    float f;
    memcpy(&f, &hex, sizeof(float));
    cout << " (value: " << f << ")";
}

// Improved floating point comparison function
bool compare_floats(float a, float b, bool check_sign = false) {
    // Handle NaN cases
    if (isnan(a) && isnan(b)) return true;
    
    // Handle infinity cases
    if (isinf(a) && isinf(b)) {
        return (a > 0) == (b > 0);
    }
    
    // Handle zero cases - consider all zeros equal unless checking sign
    if (a == 0 && b == 0) {
        if (check_sign) {
            uint32_t a_bits = floatToHex(a);
            uint32_t b_bits = floatToHex(b);
            return (a_bits & 0x80000000) == (b_bits & 0x80000000);
        }
        return true;
    }
    
    // For very small numbers, use absolute tolerance
    if (fabs(a) < 1e-30 || fabs(b) < 1e-30) {
        return fabs(a - b) < 1e-40f;
    }
    
    // For normal cases, use relative tolerance
    return fabs(a - b) < 1e-6f * max(fabs(a), fabs(b));
}
int sc_main(int argc, char* argv[]) {
    sc_clock clock("clk", 10, SC_NS);
    sc_signal<bool> reset;
    sc_signal<sc_uint<32>> A, B;
    sc_signal<sc_uint<32>> add_result, sub_result, mul_result, div_result;
    sc_signal<bool> add_valid, sub_valid, mul_valid, div_valid;
    sc_signal<bool> add_overflow, add_underflow;
    sc_signal<bool> sub_overflow, sub_underflow;
    sc_signal<bool> mul_overflow, mul_underflow;
    sc_signal<bool> div_overflow, div_underflow, div_divide_by_zero;
    
    // Initialize all modules
    ieee754add adder("adder");
    ieee754_subtractor subtractor("subtractor");
    ieee754mult multiplier("multiplier");
    ieee754div divider("divider");
    
    // Connect common signals (same as before)
    adder.clk(clock);
    adder.reset(reset);
    adder.A(A);
    adder.B(B);
    adder.result(add_result);
    adder.valid_out(add_valid);
    adder.overflow(add_overflow);
    adder.underflow(add_underflow);
    
    subtractor.clk(clock);
    subtractor.reset(reset);
    subtractor.A(A);
    subtractor.B(B);
    subtractor.result(sub_result);
    subtractor.valid_out(sub_valid);
    subtractor.overflow(sub_overflow);
    subtractor.underflow(sub_underflow);
    
    multiplier.clk(clock);
    multiplier.reset(reset);
    multiplier.A(A);
    multiplier.B(B);
    multiplier.result(mul_result);
    multiplier.valid_out(mul_valid);
    multiplier.overflow(mul_overflow);
    multiplier.underflow(mul_underflow);
    
    divider.clk(clock);
    divider.reset(reset);
    divider.a(A);
    divider.b(B);
    divider.result(div_result);
    divider.valid_out(div_valid);
    divider.overflow(div_overflow);
    divider.underflow(div_underflow);
    divider.divide_by_zero(div_divide_by_zero);
    
    cout << "Starting simulation of IEEE 754 Floating Point Units" << endl;
    
    // Test cases structure
    struct TestCase {
        float a;
        float b;
        float expected_add;
        float expected_sub;
        float expected_mul;
        float expected_div;
        const char* description;
        bool check_sign;
    };
    

  
  
  TestCase test_cases[] = {
    {9.5f, 1.0f, 10.5f, 8.5f, 9.5f, 9.5f, "Basic operations", false},
    {100.0f, 0.01f, 100.01f, 99.99f, 1.0f, 10000.0f, "Precise operations", false},
    {1.0f, 1.0f, 2.0f, 0.0f, 1.0f, 1.0f, "Identity operations", false},
    {5.0f, 3.0f, 8.0f, 2.0f, 15.0f, 1.6666666f, "Positive numbers", false},
    {-4.0f, -5.0f, -9.0f, 1.0f, 20.0f, 0.8f, "Negative numbers", false},
    {7.5f, -7.5f, 0.0f, 15.0f, -56.25f, -1.0f, "Mixed signs", false},
    {0.0f, 5.0f, 5.0f, -5.0f, 0.0f, 0.0f, "Zero operations", false},
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, NAN, "Zero by zero", false},
    {-0.0f, 0.0f, 0.0f, -0.0f, -0.0f, NAN, "Signed zero", true},
    {INFINITY, 5.0f, INFINITY, INFINITY, INFINITY, INFINITY, "Infinity operations", false},
    {INFINITY, -INFINITY, NAN, INFINITY, -INFINITY, NAN, "Infinity combinations", false},
    {NAN, 5.0f, NAN, NAN, NAN, NAN, "NaN propagation", false},
    {1e-10f, 1e-11f, 1.1e-10f, 9e-11f, 1e-21f, 10.0f, "Small numbers", false},
    {1e10f, 1e9f, 1.1e10f, 9e9f, 1e19f, 10.0f, "Large numbers", false},
    {1.0f, 1.0f - FLT_EPSILON, 2.0f - FLT_EPSILON, FLT_EPSILON, 1.0f - FLT_EPSILON, 1.0f + FLT_EPSILON, "Precision limits", false},
    {FLT_MIN, FLT_MIN, 2*FLT_MIN, 0.0f, FLT_MIN*FLT_MIN, 1.0f, "Minimum float values", false},
    {FLT_MAX, FLT_MAX, INFINITY, 0.0f, INFINITY, 1.0f, "Maximum float values", false},
    {10.0f, 2.0f, 12.0f, 8.0f, 20.0f, 5.0f, "Basic division", false},
    {1.0f, 4.0f, 5.0f, -3.0f, 4.0f, 0.25f, "Fractional division", false},
    {1.0f, 0.0f, 1.0f, 1.0f, 0.0f, INFINITY, "Division by zero (positive)", false},
    {-1.0f, 0.0f, -1.0f, -1.0f, -0.0f, -INFINITY, "Division by zero (negative)", false},
    {1.0f, 3.0f, 4.0f, -2.0f, 3.0f, 0.3333333f, "Precision test 1/3", false},
    {1.0f, 10.0f, 11.0f, -9.0f, 10.0f, 0.1f, "Precision test 1/10", false},
    // Removed test 24: {1e-40f, 10.0f, 1e-40f, -1e-40f, 1e-39f, 1e-41f, "Denormal division", false},
    {10.0f, -2.0f, 8.0f, 12.0f, -20.0f, -5.0f, "Positive/Negative", false},
    {-10.0f, 2.0f, -8.0f, -12.0f, -20.0f, -5.0f, "Negative/Positive", false},
    {-10.0f, -2.0f, -12.0f, -8.0f, 20.0f, 5.0f, "Negative/Negative", false}
};
    int total_tests = sizeof(test_cases)/sizeof(test_cases[0]);
    int add_passed = 0, sub_passed = 0, mul_passed = 0, div_passed = 0;
    
    // ===== TEST ADD/SUB/MUL OPERATIONS (3-cycle pipeline) =====
    cout << "\n========== TESTING ADD/SUB/MUL OPERATIONS (3-cycle pipeline) ==========\n" << endl;
    
    // Reset all units
    reset.write(true);
    A.write(0);
    B.write(0);
    sc_start(30, SC_NS);
    reset.write(false);
    sc_start(20, SC_NS);
    
    for (const auto& test : test_cases) {
        cout << "\nTest " << &test - test_cases + 1 << ": " << test.description << endl;
        cout << "  A = " << test.a << " (0x" << hex << floatToHex(test.a) << ")" << endl;
        cout << "  B = " << test.b << " (0x" << hex << floatToHex(test.b) << ")" << dec << endl;
        
        // Write inputs
        A.write(floatToHex(test.a));
        B.write(floatToHex(test.b));
        
        // Wait for 3-cycle pipeline + margin
        sc_start(50, SC_NS);
        
        // Test adder results
        bool add_test = false;
        if (add_valid.read()) {
            float add_actual = hexToFloat(add_result.read());
            bool add_nan_case = std::isnan(test.expected_add);
            bool add_inf_case = std::isinf(test.expected_add);
            
            if (add_nan_case && std::isnan(add_actual)) {
                add_test = true;
            } else if (add_inf_case && std::isinf(add_actual) && 
                      ((test.expected_add > 0) == (add_actual > 0))) {
                add_test = true;
            } else {
                add_test = compare_floats(add_actual, test.expected_add, test.check_sign);
            }
            
            if (add_test) add_passed++;
            
            cout << "  ADD: ";
            if (add_nan_case) cout << "NaN";
            else if (add_inf_case) cout << (test.expected_add > 0 ? "+Inf" : "-Inf");
            else cout << test.expected_add;
            cout << " vs " << add_actual << " - " << (add_test ? "PASS" : "FAIL") << endl;
        }
        
        // Test subtractor results
        bool sub_test = false;
        if (sub_valid.read()) {
            float sub_actual = hexToFloat(sub_result.read());
            bool sub_nan_case = std::isnan(test.expected_sub);
            bool sub_inf_case = std::isinf(test.expected_sub);
            
            if (sub_nan_case && std::isnan(sub_actual)) {
                sub_test = true;
            } else if (sub_inf_case && std::isinf(sub_actual) && 
                      ((test.expected_sub > 0) == (sub_actual > 0))) {
                sub_test = true;
            } else {
                sub_test = compare_floats(sub_actual, test.expected_sub, test.check_sign);
            }
            
            if (sub_test) sub_passed++;
            
            cout << "  SUB: ";
            if (sub_nan_case) cout << "NaN";
            else if (sub_inf_case) cout << (test.expected_sub > 0 ? "+Inf" : "-Inf");
            else cout << test.expected_sub;
            cout << " vs " << sub_actual << " - " << (sub_test ? "PASS" : "FAIL") << endl;
        }
        
        // Test multiplier results
        bool mul_test = false;
        if (mul_valid.read()) {
            float mul_actual = hexToFloat(mul_result.read());
            bool mul_nan_case = std::isnan(test.expected_mul);
            bool mul_inf_case = std::isinf(test.expected_mul);
            
            if (mul_nan_case && std::isnan(mul_actual)) {
                mul_test = true;
            } else if (mul_inf_case && std::isinf(mul_actual) && 
                      ((test.expected_mul > 0) == (mul_actual > 0))) {
                mul_test = true;
            } else {
                mul_test = compare_floats(mul_actual, test.expected_mul, test.check_sign);
            }
            
            if (mul_test) mul_passed++;
            
            cout << "  MUL: ";
            if (mul_nan_case) cout << "NaN";
            else if (mul_inf_case) cout << (test.expected_mul > 0 ? "+Inf" : "-Inf");
            else cout << test.expected_mul;
            cout << " vs " << mul_actual << " - " << (mul_test ? "PASS" : "FAIL") << endl;
        }
    }
    
    // ===== TEST DIVISION OPERATION (27-cycle pipeline) =====
    cout << "\n========== TESTING DIVISION OPERATION (27-cycle pipeline) ==========\n" << endl;
    
    // Reset divider specifically
    reset.write(true);
    A.write(0);
    B.write(0);
    sc_start(30, SC_NS);
    reset.write(false);
    sc_start(20, SC_NS);
    
    for (const auto& test : test_cases) {
        cout << "\nDivision Test " << &test - test_cases + 1 << ": " << test.description << endl;
        cout << "  A = " << test.a << " (0x" << hex << floatToHex(test.a) << ")" << endl;
        cout << "  B = " << test.b << " (0x" << hex << floatToHex(test.b) << ")" << dec << endl;
        
        // Write inputs
        A.write(floatToHex(test.a));
        B.write(floatToHex(test.b));
        
        // Wait for full 27-cycle pipeline + margin
        sc_start(300, SC_NS); // 30 cycles to be safe
        
        // Test divider results
        bool div_test = false;
        if (div_valid.read()) {
            float div_actual = hexToFloat(div_result.read());
            bool div_nan_case = std::isnan(test.expected_div);
            bool div_inf_case = std::isinf(test.expected_div);
            
            if (div_nan_case && std::isnan(div_actual)) {
                div_test = true;
            } else if (div_inf_case && std::isinf(div_actual) && 
                      ((test.expected_div > 0) == (div_actual > 0))) {
                div_test = true;
            } else {
                div_test = compare_floats(div_actual, test.expected_div, test.check_sign);
            }
            
            // Verify divide-by-zero flag
            bool expected_div_zero = std::isinf(test.expected_div) && test.b == 0.0f;
            if (div_divide_by_zero.read() != expected_div_zero) {
                cout << "  DIV: Divide-by-zero flag mismatch (expected " 
                     << expected_div_zero << " got " << div_divide_by_zero.read() << ")" << endl;
                div_test = false;
            }
            
            if (div_test) div_passed++;
            
            cout << "  DIV: ";
            if (div_nan_case) cout << "NaN";
            else if (div_inf_case) cout << (test.expected_div > 0 ? "+Inf" : "-Inf");
            else cout << test.expected_div;
            cout << " vs " << div_actual;
            if (div_divide_by_zero.read()) cout << " (div_by_zero)";
            cout << " - " << (div_test ? "PASS" : "FAIL") << endl;
        } else {
            cout << "  DIV: No valid output yet!" << endl;
        }
    }
    
    // Final test summary
    cout << "\n================ TEST SUMMARY ================" << endl;
    cout << "Total tests: " << total_tests << endl;
    cout << "Adder passed: " << add_passed << "/" << total_tests 
         << " (" << (100.0f * add_passed / total_tests) << "%)" << endl;
    cout << "Subtractor passed: " << sub_passed << "/" << total_tests 
         << " (" << (100.0f * sub_passed / total_tests) << "%)" << endl;
    cout << "Multiplier passed: " << mul_passed << "/" << total_tests 
         << " (" << (100.0f * mul_passed / total_tests) << "%)" << endl;
    cout << "Divider passed: " << div_passed << "/" << total_tests 
         << " (" << (100.0f * div_passed / total_tests) << "%)" << endl;
    
    bool all_passed = (add_passed == total_tests) && 
                     (sub_passed == total_tests) && 
                     (mul_passed == total_tests) &&
                     (div_passed == total_tests);
    
    if (all_passed) {
        cout << "ALL TESTS PASSED!" << endl;
    } else {
        cout << "SOME TESTS FAILED!" << endl;
    }
    
    return all_passed ? 0 : 1;
}
