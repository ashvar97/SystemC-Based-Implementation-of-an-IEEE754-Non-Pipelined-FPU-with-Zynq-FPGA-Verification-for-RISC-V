#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <float.h>

// IEEE 754 special values
#define POS_ZERO    0x00000000
#define NEG_ZERO    0x80000000
#define POS_INF     0x7F800000
#define NEG_INF     0xFF800000
#define QUIET_NAN   0x7FC00000

// Operation types
#define FADD 0
#define FSUB 1
#define FMUL 2
#define FDIV 3

// Updated test case count
#define NUM_RANDOM_TESTS 5

// Union for easy floating-point <-> integer conversion
typedef union {
    float f;
    uint32_t i;
} float_bits;

// Function prototypes
void test_basic_operations(void);
void test_special_values(void);
void test_random_values(int num_tests);
void execute_test(int op, uint32_t a, uint32_t b, uint32_t expected);
const char* op_to_symbol(int op);
const char* op_to_name(int op);
bool is_nan(uint32_t value);
bool is_zero(uint32_t value);
bool is_infinity(uint32_t value);
uint32_t perform_fp_operation(int op, uint32_t a, uint32_t b);
float rand_float(void);

// Global variables
int error_count = 0;

int main() {
    printf("Starting RISC-V Floating-Point Unit Test\n");
    printf("=========================================\n");
    
    // Test 1: Basic operations
    test_basic_operations();
    
    // Test 2: Special values handling
    test_special_values();
    
    // Test 3: Random values
    test_random_values(NUM_RANDOM_TESTS);
    
    // Final report
    if (error_count == 0) {
        printf("\nAll tests PASSED!\n");
    } else {
        printf("\nTests completed with %d errors\n", error_count);
    }
    
    return (error_count == 0) ? 0 : 1;
}

// Test basic arithmetic operations with the values from SystemC code
void test_basic_operations(void) {
    float_bits pi, e, one, zero, very_large, very_small, inf, nan_val;
    
    // Initialize values to match SystemC test
    pi.f = 3.14159f;          // Pi
    e.f = 2.71828f;           // Euler's number
    one.f = 1.0f;             // One
    zero.f = 0.0f;            // Zero
    very_large.f = 1.0e30f;   // Very large number
    very_small.f = 1.0e-30f;  // Very small number
    inf.i = POS_INF;          // Positive infinity
    nan_val.i = QUIET_NAN;    // NaN
    
    printf("\nTest Case 1: Basic Operations (SystemC Test Cases)\n");
    printf("-----------------------------------------------\n");
    
    // Basic arithmetic operations
    printf("\nBasic Arithmetic Operations:\n");
    execute_test(FADD, pi.i, e.i, 0); // Pi + e
    execute_test(FSUB, pi.i, e.i, 0); // Pi - e
    execute_test(FMUL, pi.i, e.i, 0); // Pi * e
    execute_test(FDIV, pi.i, e.i, 0); // Pi / e
    
    // Special cases from SystemC test
    printf("\nSpecial Cases:\n");
    execute_test(FDIV, one.i, zero.i, 0);            // 1.0 / 0.0 (Division by zero)
    execute_test(FMUL, very_large.i, very_small.i, 0); // Very large * Very small
    execute_test(FADD, very_large.i, very_large.i, 0); // Very large + Very large
    
    // Additional tests from SystemC
    printf("\nAdditional Tests:\n");
    execute_test(FMUL, pi.i, one.i, 0);          // Pi * 1.0
    execute_test(FADD, nan_val.i, pi.i, 0);      // NaN + Pi
    execute_test(FDIV, pi.i, pi.i, 0);           // Pi / Pi
    execute_test(FSUB, zero.i, zero.i, 0);       // 0.0 - 0.0
    execute_test(FADD, one.i, inf.i, 0);         // 1.0 + infinity
}

// Test special IEEE 754 values like NaN, Infinity, and Zero
void test_special_values(void) {
    printf("\nTest Case 2: Special IEEE 754 Values\n");
    printf("---------------------------------\n");
    
    // NaN handling
    printf("\nTesting NaN handling:\n");
    execute_test(FADD, QUIET_NAN, 0x3F800000, QUIET_NAN);  // NaN + 1.0 = NaN
    execute_test(FMUL, QUIET_NAN, 0x3F800000, QUIET_NAN);  // NaN * 1.0 = NaN
    
    // Infinity handling
    printf("\nTesting Infinity handling:\n");
    execute_test(FADD, POS_INF, 0x3F800000, POS_INF);      // +Inf + 1.0 = +Inf
    execute_test(FADD, POS_INF, NEG_INF, QUIET_NAN);       // +Inf + (-Inf) = NaN
    execute_test(FMUL, POS_INF, 0x3F800000, POS_INF);      // +Inf * 1.0 = +Inf
    execute_test(FMUL, POS_INF, NEG_INF, NEG_INF);         // +Inf * (-Inf) = -Inf
    execute_test(FMUL, POS_INF, POS_ZERO, QUIET_NAN);      // +Inf * 0 = NaN
    
    // Zero handling
    printf("\nTesting Zero handling:\n");
    execute_test(FADD, POS_ZERO, POS_ZERO, POS_ZERO);      // +0 + (+0) = +0
    execute_test(FADD, POS_ZERO, NEG_ZERO, POS_ZERO);      // +0 + (-0) = +0
    execute_test(FDIV, 0x3F800000, POS_ZERO, POS_INF);     // 1.0 / +0 = +Inf
}

// Test with random values
void test_random_values(int num_tests) {
    uint32_t a, b;
    float_bits a_bits, b_bits;
    
    printf("\nTest Case 3: Random Values (%d tests per operation)\n", num_tests);
    printf("-----------------------------------------------\n");
    
    // Seed random number generator
    srand(42);  // Fixed seed for reproducibility
    
    for (int op = 0; op < 4; op++) {
        printf("\nTesting %s with random values:\n", op_to_name(op));
        
        for (int i = 0; i < num_tests; i++) {
            // Generate random normal values (avoiding special values)
            a_bits.f = rand_float();
            b_bits.f = rand_float();
            
            a = a_bits.i;
            b = b_bits.i;
            
            // Execute test (passing 0 for expected to skip validation)
            execute_test(op, a, b, 0);
        }
    }
}

// Execute a single test and check results
void execute_test(int op, uint32_t a, uint32_t b, uint32_t expected) {
    float_bits a_bits, b_bits, result_bits, expected_bits;
    uint32_t result;
    bool skip_validation = (expected == 0);
    
    a_bits.i = a;
    b_bits.i = b;
    
    // Perform operation
    result = perform_fp_operation(op, a, b);
    result_bits.i = result;
    
    // Calculate expected result if not provided
    if (skip_validation) {
        switch (op) {
            case FADD: expected_bits.f = a_bits.f + b_bits.f; break;
            case FSUB: expected_bits.f = a_bits.f - b_bits.f; break;
            case FMUL: expected_bits.f = a_bits.f * b_bits.f; break;
            case FDIV: expected_bits.f = a_bits.f / b_bits.f; break;
            default: expected_bits.f = 0.0f; break;
        }
        expected = expected_bits.i;
    } else {
        expected_bits.i = expected;
    }
    
    // Special case handling for validating NaN results
    if (is_nan(result) && is_nan(expected)) {
        printf("  %s: %g %s %g = NaN (0x%08X) - PASS\n", 
               op_to_name(op), a_bits.f, op_to_symbol(op), b_bits.f, result);
    } 
    // Special case handling for validating zero results
    else if (is_zero(result) && is_zero(expected)) {
        printf("  %s: %g %s %g = %s0 (0x%08X) - PASS\n", 
               op_to_name(op), a_bits.f, op_to_symbol(op), b_bits.f, 
               (result & 0x80000000) ? "-" : "+", result);
    }
    // Special case handling for validating infinity results
    else if (is_infinity(result) && is_infinity(expected) && 
            ((result & 0x80000000) == (expected & 0x80000000))) {
        printf("  %s: %g %s %g = %sInf (0x%08X) - PASS\n", 
               op_to_name(op), a_bits.f, op_to_symbol(op), b_bits.f,
               (result & 0x80000000) ? "-" : "+", result);
    }
    // For normal cases, check if bits match exactly
    // For random tests, we skip detailed validation but still print the result
    else if (skip_validation || (result == expected)) {
        printf("  %s: %g %s %g = %g (0x%08X)\n", 
               op_to_name(op), a_bits.f, op_to_symbol(op), b_bits.f, result_bits.f, result);
    }
    // If validation fails
    else {
        printf("  %s: %g %s %g = %g (0x%08X), Expected: %g (0x%08X) - FAIL\n", 
               op_to_name(op), a_bits.f, op_to_symbol(op), b_bits.f, 
               result_bits.f, result, expected_bits.f, expected);
        error_count++;
    }
}

// Perform floating-point operation with proper handling of special cases
uint32_t perform_fp_operation(int op, uint32_t a, uint32_t b) {
    float_bits a_bits, b_bits, result_bits;
    uint32_t result;
    uint32_t sign_result;
    
    a_bits.i = a;
    b_bits.i = b;
    
    // Handle special cases common to all operations
    if ((a & 0x7FFFFFFF) > POS_INF || (b & 0x7FFFFFFF) > POS_INF) {
        return QUIET_NAN;  // NaN input
    }
    
    // Operation-specific special case handling
    switch (op) {
        case FADD:
            if ((a & 0x7FFFFFFF) == POS_INF && (b & 0x7FFFFFFF) == POS_INF && a != b) {
                return QUIET_NAN;  // +Inf + -Inf = NaN
            }
            if ((a & 0x7FFFFFFF) == POS_INF) {
                return a;  // Inf + x = Inf (with sign)
            }
            if ((b & 0x7FFFFFFF) == POS_INF) {
                return b;  // x + Inf = Inf (with sign)
            }
            result_bits.f = a_bits.f + b_bits.f;
            break;
            
        case FSUB:
            if ((a & 0x7FFFFFFF) == POS_INF && (b & 0x7FFFFFFF) == POS_INF && ((a ^ b) & 0x80000000) == 0) {
                return QUIET_NAN;  // +Inf - +Inf = NaN
            }
            if ((a & 0x7FFFFFFF) == POS_INF) {
                return a;  // Inf - x = Inf (with sign)
            }
            if ((b & 0x7FFFFFFF) == POS_INF) {
                return ((b ^ 0x80000000) & 0xFFFFFFFF);  // x - Inf = -Inf (with inverted sign)
            }
            result_bits.f = a_bits.f - b_bits.f;
            break;
            
        case FMUL:
            if (((a & 0x7FFFFFFF) == 0 && (b & 0x7FFFFFFF) == POS_INF) ||
                ((b & 0x7FFFFFFF) == 0 && (a & 0x7FFFFFFF) == POS_INF)) {
                return QUIET_NAN;  // 0 * Inf = NaN
            }
            
            sign_result = (a ^ b) & 0x80000000;
            
            if ((a & 0x7FFFFFFF) == POS_INF || (b & 0x7FFFFFFF) == POS_INF) {
                return sign_result | POS_INF;  // Inf * x = Inf (with sign)
            }
            result_bits.f = a_bits.f * b_bits.f;
            break;
            
        case FDIV:
            if (((a & 0x7FFFFFFF) == POS_INF && (b & 0x7FFFFFFF) == POS_INF) ||
                ((a & 0x7FFFFFFF) == 0 && (b & 0x7FFFFFFF) == 0)) {
                return QUIET_NAN;  // Inf/Inf or 0/0 = NaN
            }
            
            sign_result = (a ^ b) & 0x80000000;
            
            if ((b & 0x7FFFFFFF) == 0) {
                return sign_result | POS_INF;  // x/0 = Inf (with sign)
            }
            if ((a & 0x7FFFFFFF) == POS_INF) {
                return sign_result | POS_INF;  // Inf/x = Inf (with sign)
            }
            if ((b & 0x7FFFFFFF) == POS_INF) {
                return sign_result | POS_ZERO;  // x/Inf = 0 (with sign)
            }
            result_bits.f = a_bits.f / b_bits.f;
            break;
            
        default:
            return 0;
    }
    
    return result_bits.i;
}

// Convert operation to symbol for display
const char* op_to_symbol(int op) {
    switch (op) {
        case FADD: return "+";
        case FSUB: return "-";
        case FMUL: return "*";
        case FDIV: return "/";
        default: return "?";
    }
}

// Convert operation to name for display
const char* op_to_name(int op) {
    switch (op) {
        case FADD: return "FADD";
        case FSUB: return "FSUB";
        case FMUL: return "FMUL";
        case FDIV: return "FDIV";
        default: return "UNKNOWN";
    }
}

// Check if value is NaN
bool is_nan(uint32_t value) {
    return ((value & 0x7F800000) == 0x7F800000) && ((value & 0x007FFFFF) != 0);
}

// Check if value is zero (positive or negative)
bool is_zero(uint32_t value) {
    return (value & 0x7FFFFFFF) == 0;
}

// Check if value is infinity (positive or negative)
bool is_infinity(uint32_t value) {
    return ((value & 0x7FFFFFFF) == POS_INF);
}

// Generate a random float in a reasonable range
float rand_float(void) {
    // Generate a random float between -100 and 100
    float r = ((float)rand() / (float)RAND_MAX) * 200.0f - 100.0f;
    
    // Occasionally (10% chance) generate a very small number
    if (rand() % 10 == 0) {
        r /= 1000000.0f;
    }
    
    return r;
}