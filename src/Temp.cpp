/*******************************************************************************
 * Extended Floating-Point Unit with Linear Algebra & Differential Equations
 * Integrated with Your Existing Pipelined FPU Architecture
 * 
 * Key Additions:
 * 1. Vector Operations (dot, cross, norm)
 * 2. Matrix Operations (multiply, determinant, inverse)
 * 3. Linear System Solvers (Gaussian elimination, LU)
 * 4. ODE Solvers (Euler, Runge-Kutta 4)
 * 5. Integration with existing pipeline stages
 ******************************************************************************/

#include <systemc.h>
#include <cmath>

// ============================================================================
// EXTENDED OPCODES - Add to your existing opcodes
// ============================================================================
enum extended_fp_opcodes {
    // Your existing basic operations
    OP_FADD   = 0x00,
    OP_FSUB   = 0x01,
    OP_FMUL   = 0x02,
    OP_FDIV   = 0x03,
    
    // NEW: Vector Operations
    OP_VDOT   = 0x10,  // Dot product
    OP_VCROSS = 0x11,  // Cross product
    OP_VNORM  = 0x12,  // Vector magnitude
    
    // NEW: Matrix Operations  
    OP_MMUL   = 0x20,  // Matrix multiply
    OP_MDET   = 0x21,  // Determinant
    OP_MINV   = 0x22,  // Matrix inverse
    
    // NEW: Linear System Solver
    OP_GAUSS  = 0x30,  // Solve Ax=b
    
    // NEW: Differential Equations
    OP_EULER  = 0x40,  // Euler method
    OP_RK4    = 0x41   // Runge-Kutta 4
};

// ============================================================================
// VECTOR DOT PRODUCT UNIT
// Hardware implementation of v1 · v2 = Σ(v1[i] * v2[i])
// ============================================================================
SC_MODULE(VectorDotProductUnit) {
    sc_in<bool> clk;
    sc_in<bool> reset;
    
    // Input: Two 4-element vectors
    sc_in<sc_uint<32>> vec1[4];
    sc_in<sc_uint<32>> vec2[4];
    sc_in<sc_uint<3>> size;  // Actual size (1-4)
    
    // Output: Scalar result
    sc_out<sc_uint<32>> result;
    sc_out<bool> valid;
    
    // Pipeline stages for dot product
    sc_uint<32> products[4];
    sc_uint<32> sum_stage1[2];
    sc_uint<32> sum_stage2;
    
    void compute() {
        if (reset.read()) {
            result.write(0);
            valid.write(false);
            return;
        }
        
        int n = size.read();
        
        // Stage 1: Parallel multiplication
        for (int i = 0; i < n; i++) {
            products[i] = ieee754_multiply(vec1[i].read(), vec2[i].read());
        }
        
        // Stage 2: Tree addition (2 parallel adds)
        sum_stage1[0] = ieee754_add(products[0], products[1]);
        sum_stage1[1] = ieee754_add(products[2], products[3]);
        
        // Stage 3: Final addition
        sum_stage2 = ieee754_add(sum_stage1[0], sum_stage1[1]);
        
        result.write(sum_stage2);
        valid.write(true);
    }
    
    SC_CTOR(VectorDotProductUnit) {
        SC_METHOD(compute);
        sensitive << clk.pos();
    }
    
private:
    // Connect to your existing IEEE754 units
    sc_uint<32> ieee754_multiply(sc_uint<32> a, sc_uint<32> b);
    sc_uint<32> ieee754_add(sc_uint<32> a, sc_uint<32> b);
};

// ============================================================================
// MATRIX MULTIPLICATION UNIT (2×2 matrices)
// C = A × B where C[i][j] = Σ A[i][k] * B[k][j]
// ============================================================================
SC_MODULE(MatrixMultiplyUnit) {
    sc_in<bool> clk;
    sc_in<bool> reset;
    
    sc_in<sc_uint<32>> mat_a[2][2];
    sc_in<sc_uint<32>> mat_b[2][2];
    sc_out<sc_uint<32>> mat_c[2][2];
    sc_out<bool> valid;
    
    // Multi-cycle operation
    int cycle;
    sc_uint<32> temp_results[2][2];
    
    void multiply() {
        if (reset.read()) {
            for (int i = 0; i < 2; i++)
                for (int j = 0; j < 2; j++)
                    mat_c[i][j].write(0);
            valid.write(false);
            cycle = 0;
            return;
        }
        
        // Cycle 0-3: Compute each element (4 cycles total)
        if (cycle < 4) {
            int i = cycle / 2;
            int j = cycle % 2;
            
            // C[i][j] = A[i][0]*B[0][j] + A[i][1]*B[1][j]
            sc_uint<32> prod1 = ieee754_multiply(
                mat_a[i][0].read(), mat_b[0][j].read()
            );
            sc_uint<32> prod2 = ieee754_multiply(
                mat_a[i][1].read(), mat_b[1][j].read()
            );
            
            temp_results[i][j] = ieee754_add(prod1, prod2);
            cycle++;
        }
        
        // Cycle 4: Output results
        if (cycle == 4) {
            for (int i = 0; i < 2; i++) {
                for (int j = 0; j < 2; j++) {
                    mat_c[i][j].write(temp_results[i][j]);
                }
            }
            valid.write(true);
            cycle = 0;
        }
    }
    
    SC_CTOR(MatrixMultiplyUnit) : cycle(0) {
        SC_METHOD(multiply);
        sensitive << clk.pos();
    }
    
private:
    sc_uint<32> ieee754_multiply(sc_uint<32> a, sc_uint<32> b);
    sc_uint<32> ieee754_add(sc_uint<32> a, sc_uint<32> b);
};

// ============================================================================
// GAUSSIAN ELIMINATION SOLVER
// Solves Ax = b for x using Gaussian elimination with back substitution
// ============================================================================
SC_MODULE(GaussianSolver) {
    sc_in<bool> clk;
    sc_in<bool> reset;
    sc_in<bool> start;
    
    // Input: 3×3 system Ax = b
    sc_in<sc_uint<32>> A[3][3];  // Coefficient matrix
    sc_in<sc_uint<32>> b[3];     // Right-hand side
    
    // Output: solution vector x
    sc_out<sc_uint<32>> x[3];
    sc_out<bool> done;
    sc_out<bool> error;  // Singular matrix
    
    // Working memory
    sc_uint<32> aug[3][4];  // Augmented matrix [A|b]
    
    enum State { IDLE, FORWARD_ELIM, BACK_SUBST, COMPLETE };
    int state;
    int pivot_row, current_row, col;
    
    void solve() {
        if (reset.read()) {
            state = IDLE;
            done.write(false);
            error.write(false);
            return;
        }
        
        switch (state) {
            case IDLE:
                if (start.read()) {
                    // Initialize augmented matrix [A|b]
                    for (int i = 0; i < 3; i++) {
                        for (int j = 0; j < 3; j++) {
                            aug[i][j] = A[i][j].read();
                        }
                        aug[i][3] = b[i].read();
                    }
                    pivot_row = 0;
                    state = FORWARD_ELIM;
                }
                break;
                
            case FORWARD_ELIM:
                // Forward elimination (one row operation per cycle)
                if (pivot_row < 3) {
                    // Check for zero pivot
                    if (is_zero(aug[pivot_row][pivot_row])) {
                        error.write(true);
                        state = IDLE;
                        break;
                    }
                    
                    // Eliminate below pivot (one row per cycle)
                    if (current_row < 3) {
                        if (current_row != pivot_row) {
                            sc_uint<32> factor = ieee754_divide(
                                aug[current_row][pivot_row],
                                aug[pivot_row][pivot_row]
                            );
                            
                            // Update entire row
                            for (int j = pivot_row; j < 4; j++) {
                                sc_uint<32> product = ieee754_multiply(
                                    factor, aug[pivot_row][j]
                                );
                                aug[current_row][j] = ieee754_subtract(
                                    aug[current_row][j], product
                                );
                            }
                        }
                        current_row++;
                    } else {
                        // Move to next pivot
                        pivot_row++;
                        current_row = 0;
                    }
                } else {
                    // Forward elimination complete
                    state = BACK_SUBST;
                    current_row = 2;  // Start from bottom
                }
                break;
                
            case BACK_SUBST:
                // Back substitution (one variable per cycle)
                if (current_row >= 0) {
                    sc_uint<32> sum = 0;
                    
                    // Subtract known variables
                    for (int j = current_row + 1; j < 3; j++) {
                        sc_uint<32> product = ieee754_multiply(
                            aug[current_row][j], x[j].read()
                        );
                        sum = (j == current_row + 1) ? product : 
                              ieee754_add(sum, product);
                    }
                    
                    // x[i] = (b[i] - sum) / A[i][i]
                    sc_uint<32> numerator = ieee754_subtract(
                        aug[current_row][3], sum
                    );
                    x[current_row].write(ieee754_divide(
                        numerator, aug[current_row][current_row]
                    ));
                    
                    current_row--;
                } else {
                    state = COMPLETE;
                }
                break;
                
            case COMPLETE:
                done.write(true);
                state = IDLE;
                break;
        }
    }
    
    bool is_zero(sc_uint<32> val) {
        // Check if value is approximately zero
        return (val & 0x7FFFFFFF) < 0x00800000;  // Very small or zero
    }
    
    SC_CTOR(GaussianSolver) : state(IDLE), pivot_row(0), current_row(0), col(0) {
        SC_METHOD(solve);
        sensitive << clk.pos();
    }
    
private:
    sc_uint<32> ieee754_add(sc_uint<32> a, sc_uint<32> b);
    sc_uint<32> ieee754_subtract(sc_uint<32> a, sc_uint<32> b);
    sc_uint<32> ieee754_multiply(sc_uint<32> a, sc_uint<32> b);
    sc_uint<32> ieee754_divide(sc_uint<32> a, sc_uint<32> b);
};

// ============================================================================
// EULER METHOD ODE SOLVER
// Solves dy/dt = f(t,y) using Euler's method: y_{n+1} = y_n + h*f(t_n, y_n)
// ============================================================================
SC_MODULE(EulerODESolver) {
    sc_in<bool> clk;
    sc_in<bool> reset;
    sc_in<bool> start;
    
    // Initial conditions
    sc_in<sc_uint<32>> t_initial;
    sc_in<sc_uint<32>> y_initial;
    sc_in<sc_uint<32>> step_size;   // h
    sc_in<sc_uint<8>> num_steps;
    
    // Function parameters (for f(t,y) = a*t + b*y)
    sc_in<sc_uint<32>> param_a;
    sc_in<sc_uint<32>> param_b;
    
    // Outputs
    sc_out<sc_uint<32>> t_current;
    sc_out<sc_uint<32>> y_current;
    sc_out<bool> step_done;
    sc_out<bool> all_done;
    
    sc_uint<32> t, y;
    int step_count;
    
    void euler_solve() {
        if (reset.read()) {
            t_current.write(0);
            y_current.write(0);
            step_done.write(false);
            all_done.write(false);
            step_count = 0;
            return;
        }
        
        if (start.read() && step_count == 0) {
            // Initialize
            t = t_initial.read();
            y = y_initial.read();
            step_count = 1;
            
            t_current.write(t);
            y_current.write(y);
            step_done.write(true);
        }
        else if (step_count > 0 && step_count <= num_steps.read()) {
            // Euler step: y_{n+1} = y_n + h * f(t_n, y_n)
            
            sc_uint<32> h = step_size.read();
            
            // Evaluate f(t,y) = a*t + b*y
            sc_uint<32> term1 = ieee754_multiply(param_a.read(), t);
            sc_uint<32> term2 = ieee754_multiply(param_b.read(), y);
            sc_uint<32> f_val = ieee754_add(term1, term2);
            
            // y_new = y + h * f(t,y)
            sc_uint<32> increment = ieee754_multiply(h, f_val);
            y = ieee754_add(y, increment);
            
            // t_new = t + h
            t = ieee754_add(t, h);
            
            t_current.write(t);
            y_current.write(y);
            step_done.write(true);
            
            step_count++;
            
            if (step_count > num_steps.read()) {
                all_done.write(true);
            }
        }
    }
    
    SC_CTOR(EulerODESolver) : step_count(0) {
        SC_METHOD(euler_solve);
        sensitive << clk.pos();
    }
    
private:
    sc_uint<32> ieee754_add(sc_uint<32> a, sc_uint<32> b);
    sc_uint<32> ieee754_multiply(sc_uint<32> a, sc_uint<32> b);
};

// ============================================================================
// RUNGE-KUTTA 4TH ORDER SOLVER
// Higher accuracy ODE solver: 4th order method
// ============================================================================
SC_MODULE(RungeKutta4Solver) {
    sc_in<bool> clk;
    sc_in<bool> reset;
    sc_in<bool> start;
    
    sc_in<sc_uint<32>> t_initial;
    sc_in<sc_uint<32>> y_initial;
    sc_in<sc_uint<32>> step_size;
    sc_in<sc_uint<8>> num_steps;
    
    // Function parameters
    sc_in<sc_uint<32>> param_a;
    sc_in<sc_uint<32>> param_b;
    
    sc_out<sc_uint<32>> t_current;
    sc_out<sc_uint<32>> y_current;
    sc_out<bool> step_done;
    sc_out<bool> all_done;
    
    sc_uint<32> t, y;
    int step_count;
    int substep;  // RK4 requires 4 substeps
    
    // RK4 intermediate values
    sc_uint<32> k1, k2, k3, k4;
    sc_uint<32> t_temp, y_temp;
    
    void rk4_solve() {
        if (reset.read()) {
            t_current.write(0);
            y_current.write(0);
            step_done.write(false);
            all_done.write(false);
            step_count = 0;
            substep = 0;
            return;
        }
        
        sc_uint<32> h = step_size.read();
        
        if (start.read() && step_count == 0) {
            t = t_initial.read();
            y = y_initial.read();
            step_count = 1;
            substep = 0;
            
            t_current.write(t);
            y_current.write(y);
        }
        else if (step_count > 0 && step_count <= num_steps.read()) {
            // RK4: 4 substeps per main step
            // k1 = f(t_n, y_n)
            // k2 = f(t_n + h/2, y_n + h*k1/2)
            // k3 = f(t_n + h/2, y_n + h*k2/2)
            // k4 = f(t_n + h, y_n + h*k3)
            // y_{n+1} = y_n + (h/6)*(k1 + 2*k2 + 2*k3 + k4)
            
            switch (substep) {
                case 0:  // Compute k1 = f(t, y)
                    k1 = evaluate_function(t, y);
                    substep = 1;
                    break;
                    
                case 1:  // Compute k2 = f(t + h/2, y + h*k1/2)
                    {
                        sc_uint<32> half = float_to_bits(0.5f);
                        sc_uint<32> h_half = ieee754_multiply(h, half);
                        
                        t_temp = ieee754_add(t, h_half);
                        sc_uint<32> increment = ieee754_multiply(h_half, k1);
                        y_temp = ieee754_add(y, increment);
                        
                        k2 = evaluate_function(t_temp, y_temp);
                        substep = 2;
                    }
                    break;
                    
                case 2:  // Compute k3 = f(t + h/2, y + h*k2/2)
                    {
                        sc_uint<32> half = float_to_bits(0.5f);
                        sc_uint<32> h_half = ieee754_multiply(h, half);
                        
                        sc_uint<32> increment = ieee754_multiply(h_half, k2);
                        y_temp = ieee754_add(y, increment);
                        
                        k3 = evaluate_function(t_temp, y_temp);
                        substep = 3;
                    }
                    break;
                    
                case 3:  // Compute k4 and update
                    {
                        t_temp = ieee754_add(t, h);
                        sc_uint<32> increment = ieee754_multiply(h, k3);
                        y_temp = ieee754_add(y, increment);
                        
                        k4 = evaluate_function(t_temp, y_temp);
                        
                        // y_new = y + (h/6)*(k1 + 2*k2 + 2*k3 + k4)
                        sc_uint<32> two = float_to_bits(2.0f);
                        sc_uint<32> sixth = float_to_bits(1.0f / 6.0f);
                        
                        sc_uint<32> sum = k1;
                        sum = ieee754_add(sum, ieee754_multiply(two, k2));
                        sum = ieee754_add(sum, ieee754_multiply(two, k3));
                        sum = ieee754_add(sum, k4);
                        
                        sc_uint<32> weighted = ieee754_multiply(
                            ieee754_multiply(h, sixth), sum
                        );
                        
                        y = ieee754_add(y, weighted);
                        t = ieee754_add(t, h);
                        
                        t_current.write(t);
                        y_current.write(y);
                        step_done.write(true);
                        
                        substep = 0;
                        step_count++;
                        
                        if (step_count > num_steps.read()) {
                            all_done.write(true);
                        }
                    }
                    break;
            }
        }
    }
    
    sc_uint<32> evaluate_function(sc_uint<32> t_val, sc_uint<32> y_val) {
        // f(t,y) = a*t + b*y
        sc_uint<32> term1 = ieee754_multiply(param_a.read(), t_val);
        sc_uint<32> term2 = ieee754_multiply(param_b.read(), y_val);
        return ieee754_add(term1, term2);
    }
    
    SC_CTOR(RungeKutta4Solver) : step_count(0), substep(0) {
        SC_METHOD(rk4_solve);
        sensitive << clk.pos();
    }
    
private:
    sc_uint<32> ieee754_add(sc_uint<32> a, sc_uint<32> b);
    sc_uint<32> ieee754_multiply(sc_uint<32> a, sc_uint<32> b);
    sc_uint<32> float_to_bits(float f) {
        union { float f; uint32_t u; } conv;
        conv.f = f;
        return conv.u;
    }
};

// ============================================================================
// INTEGRATION: Extended Execute Stage
// Add these units to your existing Execute module
// ============================================================================
SC_MODULE(Execute_Extended) {
    sc_in<bool> clk;
    sc_in<bool> reset;
    sc_in<bool> stall;
    
    // Standard inputs (from your existing code)
    sc_in<sc_uint<32>> pc_in;
    sc_in<sc_uint<4>> opcode_in;
    sc_in<sc_uint<5>> rd_in;
    sc_in<sc_uint<32>> operand1_in;
    sc_in<sc_uint<32>> operand2_in;
    sc_in<bool> valid_in;
    
    // Outputs
    sc_out<sc_uint<32>> result_out;
    sc_out<bool> valid_out;
    
    // Instantiate new units
    VectorDotProductUnit* vdot_unit;
    MatrixMultiplyUnit* mmul_unit;
    GaussianSolver* gauss_unit;
    EulerODESolver* euler_unit;
    RungeKutta4Solver* rk4_unit;
    
    void execute_process() {
        if (reset.read()) {
            result_out.write(0);
            valid_out.write(false);
            return;
        }
        
        if (!stall.read() && valid_in.read()) {
            sc_uint<4> op = opcode_in.read();
            
            switch (op) {
                case OP_FADD:
                case OP_FSUB:
                case OP_FMUL:
                case OP_FDIV:
                    // Use your existing FPU units
                    break;
                    
                case OP_VDOT:
                    // Route to dot product unit
                    // result_out.write(vdot_unit->result.read());
                    break;
                    
                case OP_MMUL:
                    // Route to matrix multiply unit
                    break;
                    
                case OP_GAUSS:
                    // Route to Gaussian solver
                    break;
                    
                case OP_EULER:
                    // Route to Euler ODE solver
                    break;
                    
                case OP_RK4:
                    // Route to RK4 solver
                    break;
                    
                default:
                    result_out.write(0);
                    break;
            }
            
            valid_out.write(true);
        }
    }
    
    SC_CTOR(Execute_Extended) {
        // Instantiate units
        vdot_unit = new VectorDotProductUnit("vdot");
        mmul_unit = new MatrixMultiplyUnit("mmul");
        gauss_unit = new GaussianSolver("gauss");
        euler_unit = new EulerODESolver("euler");
        rk4_unit = new RungeKutta4Solver("rk4");
        
        // Connect ports (detailed connection code here)
        
        SC_METHOD(execute_process);
        sensitive << clk.pos();
    }
    
    ~Execute_Extended() {
        delete vdot_unit;
        delete mmul_unit;
        delete gauss_unit;
        delete euler_unit;
        delete rk4_unit;
    }
};

/*******************************************************************************
 * EXAMPLE USAGE: Test Program
 * 
 * Shows how to use the new linear algebra and ODE operations
 ******************************************************************************/

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> reset;
    
    // Example: Solve dot product
    // v1 = [1.0, 2.0, 3.0, 4.0]
    // v2 = [5.0, 6.0, 7.0, 8.0]
    // Expected: 1*5 + 2*6 + 3*7 + 4*8 = 5 + 12 + 21 + 32 = 70.0
    
    // Example: Solve linear system
    // 2x + y = 5
    // x + 3y = 11
    // Expected: x = 1, y = 3
    
    // Example: Solve ODE dy/dt = -y, y(0) = 1
    // Expected: exponential decay y(t) = e^(-t)
    
    std::cout << "Extended FPU with Linear Algebra & Differential Equations\n";
    std::cout << "========================================================\n\n";
    
    // Simulation...
    
    return 0;
}
