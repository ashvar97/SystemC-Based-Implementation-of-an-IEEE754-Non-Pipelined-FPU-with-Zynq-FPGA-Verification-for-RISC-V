// Regression test for the IEEE-754 FPU modules' handling of special values: NaN, +/-Infinity,
// +/-zero, and genuine overflow/underflow (not just "normal" finite operands).
//
// This exists because the modules were originally verified only against ordinary finite float
// pairs (e.g. 1.5 + 2.5) and passed cleanly -- but had zero NaN/Inf/zero handling in
// ieee754_subtractor/ieee754mult/ieee754_div, and an 8-bit exponent computation in
// ieee754mult/ieee754_div that silently wrapped around for any operand pair with a large
// exponent gap, corrupting their own overflow/underflow checks. All of that is fixed in these
// four headers now; this test is what would have caught it, and what should catch any
// regression.
//
// Known, documented limitation: subnormal (denormal) *inputs* are not fully precision-correct
// through the adder (e.g. 1e-38 + 1e-38, since 1e-38 is itself subnormal -- min normal float is
// ~1.1754944e-38). None of the four modules attempt to produce denormalized results either;
// underflow flushes to zero. This is a deliberate scope limit, not an oversight: subnormals are
// an edge-of-edge case for the numerical/ML workloads this repo targets. NaN, Infinity, zero,
// and normal-range overflow/underflow are all fully handled and covered below.
//
// Build & run:
//   g++ -std=c++17 test_fpu_edge_cases.cpp -o test_fpu_edge_cases -lsystemc -lpthread
//   ./test_fpu_edge_cases
#include <systemc.h>
#include <cmath>
#include <cstring>
#include <string>

#include "../IEEE754Add.h"
#include "../IEEE754Sub.h"
#include "../IEEE754Mult.h"
#include "../IEEE754Div.h"

static uint32_t bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
static float unbits(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }

// Bitwise-exact comparison would fail on NaN's don't-care sign bit and payload, so special
// values are compared by classification (isnan/isinf/sign) and finite values by exact bits
// (these are all values with short, exact binary representations -- no rounding ambiguity).
static bool matches(float got, float want) {
    if (std::isnan(want)) return std::isnan(got);
    if (std::isinf(want)) return std::isinf(got) && (std::signbit(got) == std::signbit(want));
    if (want == 0.0f) return got == 0.0f && (std::signbit(got) == std::signbit(want));
    return bits(got) == bits(want);
}

SC_MODULE(EdgeCaseTB) {
    sc_signal<sc_uint<32>> a, b, add_o, sub_o, mul_o, div_o;
    sc_signal<bool> sub_en, reset;
    ieee754_adder *add_m;
    ieee754_subtractor *sub_m;
    ieee754mult *mul_m;
    ieee754_div *div_m;

    struct Case { std::string name; float x, y; };

    int failures = 0;

    void check(const char *op, const std::string &name, float got, float want) {
        if (!matches(got, want)) {
            printf("FAIL  %s(%s): got %g want %g\n", op, name.c_str(), got, want);
            failures++;
        }
    }

    void run() {
        sub_en.write(true);
        reset.write(false);

        Case cases[] = {
            {"inf+finite", INFINITY, 1.0f},
            {"inf,inf(same sign)", INFINITY, INFINITY},
            {"inf,-inf", INFINITY, -INFINITY},
            {"nan,finite", NAN, 1.0f},
            {"zero,inf", 0.0f, INFINITY},
            {"finite,zero(div)", 1.0f, 0.0f},
            {"zero,zero", 0.0f, 0.0f},
            {"overflow(mul)", 3.0e38f, 3.0e38f},
            {"neg_zero,pos_zero", -0.0f, 0.0f},
            {"large,small(no cancellation)", 1.0e30f, 1.0f},
        };

        for (auto &c : cases) {
            a.write(bits(c.x));
            b.write(bits(c.y));
            for (int i = 0; i < 20; i++) wait(SC_ZERO_TIME);

            check("add", c.name, unbits(add_o.read().to_uint()), c.x + c.y);
            check("sub", c.name, unbits(sub_o.read().to_uint()), c.x - c.y);
            check("mul", c.name, unbits(mul_o.read().to_uint()), c.x * c.y);
            check("div", c.name, unbits(div_o.read().to_uint()), c.x / c.y);
        }

        // Genuine overflow/underflow on finite operands (not driven by an Inf/zero input) --
        // this is what the widened exponent arithmetic in mult/div fixes.
        {
            a.write(bits(3.0e38f)); b.write(bits(1.0e-10f));
            for (int i = 0; i < 20; i++) wait(SC_ZERO_TIME);
            check("div", "finite overflow to inf", unbits(div_o.read().to_uint()), 3.0e38f / 1.0e-10f);
        }
        {
            // 1e-30 / 1e20 = 1e-50, below even the smallest subnormal (~1.4e-45) -- real IEEE
            // division rounds this to exact 0.0f too, so this is true underflow-to-zero, not
            // the documented flush-to-zero-instead-of-subnormal gap noted at the top of this
            // file (which only affects results that land *inside* the subnormal range).
            a.write(bits(1.0e-30f)); b.write(bits(1.0e20f));
            for (int i = 0; i < 20; i++) wait(SC_ZERO_TIME);
            check("div", "finite underflow to zero", unbits(div_o.read().to_uint()), 1.0e-30f / 1.0e20f);
        }

        printf(failures == 0 ? "\nAll edge-case checks passed.\n" : "\n%d edge-case check(s) FAILED.\n", failures);
        sc_stop();
    }

    SC_CTOR(EdgeCaseTB) {
        add_m = new ieee754_adder("add");
        add_m->A(a); add_m->B(b); add_m->O(add_o);
        sub_m = new ieee754_subtractor("sub");
        sub_m->a(a); sub_m->b(b); sub_m->enable(sub_en); sub_m->ans(sub_o);
        mul_m = new ieee754mult("mul");
        mul_m->A(a); mul_m->B(b); mul_m->reset(reset); mul_m->result(mul_o);
        div_m = new ieee754_div("div");
        div_m->a(a); div_m->b(b); div_m->reset(reset); div_m->result(div_o);
        SC_THREAD(run);
    }
};

int sc_main(int argc, char *argv[]) {
    EdgeCaseTB tb("tb");
    sc_start();
    return tb.failures == 0 ? 0 : 1;
}
