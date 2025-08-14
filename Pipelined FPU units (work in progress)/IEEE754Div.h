#include <systemc.h>
#include <iostream>
#include <cstring>
#include <cmath>

// ExtractModule: Extracts sign, exponent, and significand from floating-point inputs
SC_MODULE(ExtractModule) {
    sc_in<sc_uint<32>> a, b;
    sc_in<bool> reset;
    sc_out<sc_uint<32>> a_significand, b_significand;
    sc_out<bool> a_sign, b_sign;
    sc_out<sc_uint<8>> a_exp, b_exp;

    void extract() {
        if (reset.read()) {
            a_significand.write(0);
            b_significand.write(0);
            a_sign.write(false);
            b_sign.write(false);
            a_exp.write(0);
            b_exp.write(0);
        } else {
            // Extract biased exponents and sign bits
            a_exp.write((a.read() & 0x7F800000) >> 23);
            b_exp.write((b.read() & 0x7F800000) >> 23);
            a_sign.write((a.read() & 0x80000000) != 0);
            b_sign.write((b.read() & 0x80000000) != 0);

            // Extract significands (with implicit leading 1)
            a_significand.write((a.read() & 0x007FFFFF) | 0x00800000);
            b_significand.write((b.read() & 0x007FFFFF) | 0x00800000);
        }
    }

    SC_CTOR(ExtractModule) {
        SC_METHOD(extract);
        sensitive << a << b << reset;
    }
};

// ComputeModule: Performs floating-point division (now pipelined)
// ComputeModule: Performs floating-point division (now pipelined)
SC_MODULE(ComputeModule) {
    // Pipeline register structure
    struct PipelineRegs {
        sc_uint<32> x_val;
        sc_uint<32> y_val;
        sc_uint<32> r;
        sc_uint<8> result_exp;
        bool result_sign;
        sc_uint<5> iteration;
        bool sticky;
        bool is_zero;
        bool is_inf;
        
        bool operator == (const PipelineRegs& other) const {
            return (x_val == other.x_val) && 
                   (y_val == other.y_val) &&
                   (r == other.r) &&
                   (result_exp == other.result_exp) &&
                   (result_sign == other.result_sign) &&
                   (iteration == other.iteration) &&
                   (sticky == other.sticky) &&
                   (is_zero == other.is_zero) &&
                   (is_inf == other.is_inf);
        }
        
        friend std::ostream& operator << (std::ostream& os, const PipelineRegs& regs) {
            os << "{ x_val: " << regs.x_val 
               << ", y_val: " << regs.y_val
               << ", r: " << regs.r
               << ", result_exp: " << regs.result_exp
               << ", result_sign: " << regs.result_sign
               << ", iteration: " << regs.iteration
               << ", sticky: " << regs.sticky 
               << ", is_zero: " << regs.is_zero
               << ", is_inf: " << regs.is_inf << " }";
            return os;
        }
    };
    
    sc_in<sc_uint<32>> a_significand, b_significand;
    sc_in<bool> a_sign, b_sign;
    sc_in<sc_uint<8>> a_exp, b_exp;
    sc_in<bool> reset;
    sc_out<sc_uint<32>> result;

    // Pipeline registers
    sc_signal<PipelineRegs> regs[27]; // 25 iterations + initial + final

    void compute_pipeline() {
        if (reset.read()) {
            // Clear all pipeline registers
            for (int i = 0; i < 27; i++) {
                PipelineRegs clear_reg;
                clear_reg.x_val = 0;
                clear_reg.y_val = 0;
                clear_reg.r = 0;
                clear_reg.result_exp = 0;
                clear_reg.result_sign = false;
                clear_reg.iteration = 0;
                clear_reg.sticky = false;
                clear_reg.is_zero = false;
                clear_reg.is_inf = false;
                regs[i].write(clear_reg);
            }
            result.write(0);
        } else {
            // Stage 0: Initial setup
            PipelineRegs stage0;
            stage0.result_sign = a_sign.read() ^ b_sign.read();
            stage0.result_exp = a_exp.read() - b_exp.read() + 127;
            stage0.x_val = a_significand.read();
            stage0.y_val = b_significand.read();
            stage0.r = 0;
            stage0.iteration = 0;
            stage0.sticky = false;
            stage0.is_zero = false;
            stage0.is_inf = false;
            
            // Check for division by zero
            if (b_exp.read() == 0 && b_significand.read() == 0) {
                stage0.is_inf = true;
            }
            // Check for overflow
            else if (a_exp.read() == 255 || b_exp.read() == 0) {
                stage0.is_inf = true;
            }
            // Normal case
            else {
                // Normalize dividend if smaller than divisor
                if (stage0.x_val < stage0.y_val) {
                    stage0.x_val = stage0.x_val << 1;
                    stage0.result_exp = stage0.result_exp - 1;
                }
            }
            regs[0].write(stage0);

            // Stages 1-25: Division iterations
            for (int i = 1; i <= 25; i++) {
                PipelineRegs prev = regs[i-1].read();
                PipelineRegs current = prev;
                current.iteration = i;
                
                if (!prev.is_inf && !prev.is_zero) {
                    // Perform one iteration of division
                    current.r = prev.r << 1;
                    if (prev.x_val >= prev.y_val) {
                        current.x_val = prev.x_val - prev.y_val;
                        current.r = current.r | 1;
                    }
                    current.x_val = current.x_val << 1;
                    
                    // Carry forward sticky bit
                    current.sticky = (i == 25) ? (current.x_val != 0) : prev.sticky;
                }
                
                regs[i].write(current);
            }

            // Stage 26: Final rounding and normalization
            PipelineRegs final_stage = regs[25].read();
            sc_uint<32> r = final_stage.r;
            sc_uint<8> result_exp = final_stage.result_exp;
            bool result_sign = final_stage.result_sign;
            
            if (final_stage.is_inf) {
                // Infinity case (division by zero or overflow)
                r = 0x7F800000; // IEEE 754 infinity
            }
            else if (final_stage.is_zero) {
                // Zero case
                r = 0;
            }
            else {
                // Handle normal/overflow/underflow cases
                if ((result_exp >= 1) && (result_exp <= 254)) { // Normal case
                    bool rnd = (r & 0x1000000) >> 24;
                    bool odd = (r & 0x2) != 0;
                    r = (r >> 1) + (rnd & (final_stage.sticky | odd));
                    r = (result_exp << 23) + (r - 0x00800000);
                } 
                else if (result_exp > 254) { // Overflow to infinity
                    r = 0x7F800000;
                } 
                else { // Underflow (zero or subnormal)
                    sc_uint<8> shift = 1 - result_exp;
                    if (shift > 25) shift = 25;
                    bool sticky = final_stage.sticky | ((r & ~(~0 << shift)) != 0);
                    r = r >> shift;
                    bool rnd = (r & 0x1000000) >> 24;
                    bool odd = (r & 0x2) != 0;
                    r = (r >> 1) + (rnd & (sticky | odd));
                }
            }

            // Combine sign bit
            r = r | (result_sign ? 0x80000000 : 0);
            result.write(r);
        }
    }

    SC_CTOR(ComputeModule) {
        SC_METHOD(compute_pipeline);
        sensitive << a_significand << b_significand << a_sign << b_sign << a_exp << b_exp << reset;
        for (int i = 0; i < 27; i++) {
            sensitive << regs[i];
        }
    }
};

SC_MODULE(ieee754_div) {
    sc_in<sc_uint<32>> a, b;
    sc_in<bool> reset;
    sc_out<sc_uint<32>> result;

    // Internal signals
    sc_signal<sc_uint<32>> a_significand, b_significand;
    sc_signal<bool> a_sign, b_sign;
    sc_signal<sc_uint<8>> a_exp, b_exp;

    // Submodules
    ExtractModule extract_module;
    ComputeModule compute_module;

    SC_CTOR(ieee754_div) : 
        extract_module("extract_module"),
        compute_module("compute_module")
    {
        // Connect ExtractModule
        extract_module.a(a);
        extract_module.b(b);
        extract_module.reset(reset);
        extract_module.a_significand(a_significand);
        extract_module.b_significand(b_significand);
        extract_module.a_sign(a_sign);
        extract_module.b_sign(b_sign);
        extract_module.a_exp(a_exp);
        extract_module.b_exp(b_exp);

        // Connect ComputeModule
        compute_module.a_significand(a_significand);
        compute_module.b_significand(b_significand);
        compute_module.a_sign(a_sign);
        compute_module.b_sign(b_sign);
        compute_module.a_exp(a_exp);
        compute_module.b_exp(b_exp);
        compute_module.reset(reset);
        compute_module.result(result);
    }
};

// Testbench
int sc_main(int argc, char* argv[]) {
    sc_signal<sc_uint<32>> a, b, result;
    sc_signal<bool> reset;
    
    // Create and connect the divider
    ieee754_div divider("divider");
    divider.a(a);
    divider.b(b);
    divider.reset(reset);
    divider.result(result);
    
    // Create trace file
    sc_trace_file *tf = sc_create_vcd_trace_file("division");
    sc_trace(tf, a, "a");
    sc_trace(tf, b, "b");
    sc_trace(tf, result, "result");
    sc_trace(tf, reset, "reset");
    
    // Test cases
    struct TestCase {
        float a;
        float b;
        float expected;
    };
    
    TestCase tests[] = {
        {1.0f, 1.0f, 1.0f},
        {10.0f, 2.0f, 5.0f},
        {1.0f, 4.0f, 0.25f},
        {3.0f, 2.0f, 1.5f},
        {1.5f, 0.5f, 3.0f},
        {0.0f, 1.0f, 0.0f},
        {1.0f, 0.0f, std::numeric_limits<float>::infinity()},  // Division by zero
        {1.0e38f, 1.0e-38f, std::numeric_limits<float>::infinity()}  // Overflow
    };
    
    reset.write(true);
    sc_start(1, SC_NS);
    reset.write(false);
    
    for (const auto& test : tests) {
        // Convert floats to raw bits
        uint32_t a_bits, b_bits;
        memcpy(&a_bits, &test.a, sizeof(float));
        memcpy(&b_bits, &test.b, sizeof(float));
        
        a.write(a_bits);
        b.write(b_bits);
        
        // Run for enough cycles to complete the operation (27 cycles)
        for (int i = 0; i < 30; i++) {
            sc_start(1, SC_NS);
            
            // After pipeline is filled, we can see results
            if (i >= 27) {
                uint32_t res_bits = result.read();
                float res;
                memcpy(&res, &res_bits, sizeof(float));
                
                // Compare with expected result (with some tolerance for rounding)
                bool pass = false;
                if (std::isnan(test.expected)) {
                    pass = std::isnan(res);
                } 
                else if (std::isinf(test.expected)) {
                    pass = std::isinf(res) && (std::signbit(test.expected) == std::signbit(res));
                } 
                else {
                    float diff = std::fabs(res - test.expected);
                    float rel_diff = diff / std::fabs(test.expected);
                    pass = (rel_diff <= 1e-6) || (diff <= 1e-6);
                }
                
                std::cout << "Test " << test.a << " / " << test.b 
                     << " = " << res << " (expected " << test.expected << ")"
                     << " - " << (pass ? "PASS" : "FAIL") << std::endl;
            }
        }
    }
    
    sc_close_vcd_trace_file(tf);
    return 0;
}
