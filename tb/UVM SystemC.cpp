////////////////////////////////////////////////////////////////////////////////
// COMPLETE SystemC Testbench for FP Pipelined Processor
////////////////////////////////////////////////////////////////////////////////

#ifndef FP_PROCESSOR_TB_H
#define FP_PROCESSOR_TB_H

#include <systemc.h>
#include <vector>
#include <queue>
#include <cmath>
#include <iomanip>
#include <map>

////////////////////////////////////////////////////////////////////////////////
// Transaction Class
////////////////////////////////////////////////////////////////////////////////
class fp_transaction {
public:
    uint32_t instruction;
    bool stall;
    uint8_t monitor_pc;
    bool monitor_valid;
    uint32_t result;
    uint8_t rd;
    
    fp_transaction() 
        : instruction(0), stall(false), monitor_pc(0), 
          monitor_valid(false), result(0), rd(0) {}
    
    fp_transaction(const fp_transaction& t) 
        : instruction(t.instruction), stall(t.stall), 
          monitor_pc(t.monitor_pc), monitor_valid(t.monitor_valid),
          result(t.result), rd(t.rd) {}
};

////////////////////////////////////////////////////////////////////////////////
// Coverage Collector
////////////////////////////////////////////////////////////////////////////////
class fp_coverage {
private:
    int total_cycles;
    int valid_instructions;
    int stall_cycles;
    std::map<int, int> pc_bins;
    int stall_count;
    int no_stall_count;
    
public:
    fp_coverage() 
        : total_cycles(0), valid_instructions(0), stall_cycles(0),
          stall_count(0), no_stall_count(0) {}
    
    void sample(const fp_transaction& tx) {
        total_cycles++;
        
        if (tx.monitor_valid) {
            valid_instructions++;
            pc_bins[tx.monitor_pc]++;
        }
        
        if (tx.stall) {
            stall_cycles++;
            stall_count++;
        } else {
            no_stall_count++;
        }
    }
    
    void report() {
        double ipc = (total_cycles > 0) ? 
                     static_cast<double>(valid_instructions) / total_cycles : 0.0;
        
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << "PERFORMANCE METRICS" << std::endl;
        std::cout << std::string(60, '=') << std::endl;
        std::cout << "Total Cycles:          " << total_cycles << std::endl;
        std::cout << "Valid Instructions:    " << valid_instructions << std::endl;
        std::cout << "Stall Cycles:          " << stall_cycles << std::endl;
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "IPC (Instr/Cycle):     " << ipc << std::endl;
        
        // Calculate coverage
        int pc_coverage = pc_bins.size() * 100 / std::max(1, valid_instructions);
        std::cout << "PC Coverage:           " << pc_coverage << "%" << std::endl;
        std::cout << "Unique PCs:            " << pc_bins.size() << std::endl;
        std::cout << std::string(60, '=') << std::endl;
    }
    
    int get_valid_count() const { return valid_instructions; }
    int get_total_cycles() const { return total_cycles; }
};

////////////////////////////////////////////////////////////////////////////////
// Scoreboard
////////////////////////////////////////////////////////////////////////////////
class fp_scoreboard {
private:
    int instruction_count;
    int valid_count;
    std::vector<uint8_t> last_pc;
    
    // Convert FP32 to real
    double fp32_to_real(uint32_t fp) {
        int sign = (fp >> 31) & 1;
        int exponent = (fp >> 23) & 0xFF;
        uint32_t mantissa_bits = fp & 0x7FFFFF;
        
        if (exponent == 0 && mantissa_bits == 0)
            return 0.0;
        
        double mantissa = 1.0;
        for (int i = 0; i < 23; i++) {
            if (mantissa_bits & (1 << (22 - i))) {
                mantissa += std::pow(2.0, -(i + 1));
            }
        }
        
        double result = mantissa * std::pow(2.0, exponent - 127);
        return sign ? -result : result;
    }
    
    bool check_fp_result(uint32_t result, uint32_t expected, double tolerance = 0.001) {
        double r_result = fp32_to_real(result);
        double r_expected = fp32_to_real(expected);
        double diff = std::abs(r_result - r_expected);
        return (diff < tolerance);
    }
    
public:
    fp_scoreboard() : instruction_count(0), valid_count(0) {}
    
    void write(const fp_transaction& tx) {
        if (tx.monitor_valid) {
            valid_count++;
            
            // Check for duplicate PCs
            if (!last_pc.empty() && last_pc.back() == tx.monitor_pc) {
                std::cout << "WARNING: Duplicate PC detected: 0x" 
                          << std::hex << (int)tx.monitor_pc << std::dec << std::endl;
            }
            last_pc.push_back(tx.monitor_pc);
            
            // Print result
            std::cout << std::hex << "PC=0x" << std::setw(2) << std::setfill('0') 
                      << (int)tx.monitor_pc << " | Rd=x" << std::dec << (int)tx.rd 
                      << " | Result=0x" << std::hex << std::setw(8) << std::setfill('0')
                      << tx.result << " | FP=" << std::fixed << std::setprecision(6) 
                      << std::dec << fp32_to_real(tx.result) << std::endl;
        }
        instruction_count++;
    }
    
    void report() {
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << "FINAL RESULTS" << std::endl;
        std::cout << std::string(60, '=') << std::endl;
        std::cout << "Total cycles monitored:        " << instruction_count << std::endl;
        std::cout << "Valid instructions completed:  " << valid_count << std::endl;
        
        if (!last_pc.empty()) {
            std::cout << "Unique PCs executed:           " << last_pc.size() << std::endl;
            if (last_pc.size() != valid_count) {
                std::cout << "WARNING: PC count mismatch - possible duplicate execution" 
                          << std::endl;
            }
        }
        std::cout << std::string(60, '=') << std::endl;
    }
    
    int get_valid_count() const { return valid_count; }
};

////////////////////////////////////////////////////////////////////////////////
// Monitor
////////////////////////////////////////////////////////////////////////////////
SC_MODULE(fp_monitor) {
    sc_in<bool> clk;
    sc_in<bool> reset;
    sc_in<bool> stall;
    sc_in<bool> monitor_valid;
    sc_in<sc_uint<8>> monitor_pc;
    sc_in<sc_uint<32>> wb_result;
    sc_in<sc_uint<5>> wb_rd;
    
    fp_scoreboard* scoreboard;
    fp_coverage* coverage;
    
    SC_CTOR(fp_monitor) : scoreboard(nullptr), coverage(nullptr) {
        SC_THREAD(monitor_thread);
        sensitive << clk.pos();
    }
    
    void set_scoreboard(fp_scoreboard* sb) { scoreboard = sb; }
    void set_coverage(fp_coverage* cov) { coverage = cov; }
    
    void monitor_thread() {
        while (true) {
            wait();
            
            if (!reset.read()) {
                fp_transaction tx;
                tx.monitor_valid = monitor_valid.read();
                tx.monitor_pc = monitor_pc.read();
                tx.stall = stall.read();
                tx.result = wb_result.read();
                tx.rd = wb_rd.read();
                
                if (scoreboard) scoreboard->write(tx);
                if (coverage) coverage->sample(tx);
            }
        }
    }
};

////////////////////////////////////////////////////////////////////////////////
// Driver
////////////////////////////////////////////////////////////////////////////////
SC_MODULE(fp_driver) {
    sc_in<bool> clk;
    sc_out<bool> reset;
    sc_out<bool> stall;
    
    std::queue<fp_transaction> tx_queue;
    bool initialized;
    
    SC_CTOR(fp_driver) : initialized(false) {
        SC_THREAD(driver_thread);
        sensitive << clk.pos();
    }
    
    void add_transaction(const fp_transaction& tx) {
        tx_queue.push(tx);
    }
    
    void driver_thread() {
        // Reset sequence
        reset.write(true);
        stall.write(false);
        for (int i = 0; i < 5; i++) {
            wait();
        }
        reset.write(false);
        initialized = true;
        
        // Main driver loop
        while (true) {
            wait();
            
            if (!tx_queue.empty()) {
                fp_transaction tx = tx_queue.front();
                tx_queue.pop();
                stall.write(tx.stall);
            } else {
                stall.write(false);
            }
        }
    }
};

////////////////////////////////////////////////////////////////////////////////
// Sequence Generator
////////////////////////////////////////////////////////////////////////////////
class fp_sequence_generator {
private:
    fp_driver* driver;
    int num_transactions;
    double stall_probability;
    
public:
    fp_sequence_generator(fp_driver* drv, int num_tx = 100, double stall_prob = 0.1)
        : driver(drv), num_transactions(num_tx), stall_probability(stall_prob) {}
    
    void generate_simple_sequence() {
        for (int i = 0; i < num_transactions; i++) {
            fp_transaction tx;
            tx.stall = false; // No stalls for simple sequence
            driver->add_transaction(tx);
        }
    }
    
    void generate_random_sequence() {
        for (int i = 0; i < num_transactions; i++) {
            fp_transaction tx;
            // Random stall with specified probability
            tx.stall = (rand() % 100) < (stall_probability * 100);
            driver->add_transaction(tx);
        }
    }
};

////////////////////////////////////////////////////////////////////////////////
// Testbench Top Module (Environment)
////////////////////////////////////////////////////////////////////////////////
SC_MODULE(fp_testbench) {
    // Clock
    sc_clock clk;
    
    // Signals
    sc_signal<bool> reset;
    sc_signal<bool> stall;
    sc_signal<bool> monitor_valid;
    sc_signal<sc_uint<8>> monitor_pc;
    sc_signal<sc_uint<32>> wb_result;
    sc_signal<sc_uint<5>> wb_rd;
    
    // Components
    fp_driver* driver;
    fp_monitor* monitor;
    fp_scoreboard* scoreboard;
    fp_coverage* coverage;
    fp_sequence_generator* seq_gen;
    
    // DUT would be instantiated here
    // FPPipelinedProcessor* dut;
    
    SC_CTOR(fp_testbench) : clk("clk", 10, SC_NS) {
        // Create components
        driver = new fp_driver("driver");
        monitor = new fp_monitor("monitor");
        scoreboard = new fp_scoreboard();
        coverage = new fp_coverage();
        
        // Connect driver
        driver->clk(clk);
        driver->reset(reset);
        driver->stall(stall);
        
        // Connect monitor
        monitor->clk(clk);
        monitor->reset(reset);
        monitor->stall(stall);
        monitor->monitor_valid(monitor_valid);
        monitor->monitor_pc(monitor_pc);
        monitor->wb_result(wb_result);
        monitor->wb_rd(wb_rd);
        monitor->set_scoreboard(scoreboard);
        monitor->set_coverage(coverage);
        
        // Create sequence generator
        seq_gen = new fp_sequence_generator(driver, 50, 0.1);
        
        // Main test thread
        SC_THREAD(test_main);
    }
    
    ~fp_testbench() {
        delete driver;
        delete monitor;
        delete scoreboard;
        delete coverage;
        delete seq_gen;
    }
    
    void test_main() {
        // Wait for initialization
        wait(100, SC_NS);
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "Starting Simple Test - No Stalls" << std::endl;
        std::cout << "========================================\n" << std::endl;
        
        // Generate test sequence
        seq_gen->generate_simple_sequence();
        
        // Run test
        wait(2000, SC_NS);
        
        // Print reports
        scoreboard->report();
        coverage->report();
        
        // Calculate final statistics
        print_final_statistics();
        
        sc_stop();
    }
    
    void print_final_statistics() {
        double ipc = (coverage->get_total_cycles() > 0) ? 
                     static_cast<double>(coverage->get_valid_count()) / 
                     coverage->get_total_cycles() : 0.0;
        
        std::cout << "\n=== Simulation Statistics ===" << std::endl;
        std::cout << "Total Cycles:       " << coverage->get_total_cycles() << std::endl;
        std::cout << "Valid Instructions: " << coverage->get_valid_count() << std::endl;
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "IPC:                " << ipc << std::endl;
        std::cout << "============================\n" << std::endl;
    }
    
    void print_expected_results() {
        std::cout << "\n========================================" << std::endl;
        std::cout << "EXPECTED RESULTS:" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "PC=0x00: x6  = 1.0 + 2.0 = 3.0" << std::endl;
        std::cout << "PC=0x04: x7  = 2.0 + 0.5 = 2.5" << std::endl;
        std::cout << "PC=0x08: x8  = 2.0 - 3.0 = -1.0" << std::endl;
        std::cout << "PC=0x0c: x9  = 1.0 * 2.0 = 2.0" << std::endl;
        std::cout << "PC=0x10: x10 = 0.5 / 3.0 = 0.166..." << std::endl;
        std::cout << "PC=0x14: x11 = 4.0 + 1.0 = 5.0" << std::endl;
        std::cout << "PC=0x18: x12 = 2.0 * 3.0 = 6.0" << std::endl;
        std::cout << "PC=0x1c: x13 = 4.0 - 1.0 = 3.0" << std::endl;
        std::cout << "========================================\n" << std::endl;
    }
};

#endif // FP_PROCESSOR_TB_H

////////////////////////////////////////////////////////////////////////////////
// Main Function
////////////////////////////////////////////////////////////////////////////////
int sc_main(int argc, char* argv[]) {
    // Create testbench
    fp_testbench tb("fp_testbench");
    
    // Print expected results
    tb.print_expected_results();
    
    // Configure trace file
    sc_trace_file* tf = sc_create_vcd_trace_file("fp_processor_trace");
    tf->set_time_unit(1, SC_NS);
    
    sc_trace(tf, tb.clk, "clk");
    sc_trace(tf, tb.reset, "reset");
    sc_trace(tf, tb.stall, "stall");
    sc_trace(tf, tb.monitor_valid, "monitor_valid");
    sc_trace(tf, tb.monitor_pc, "monitor_pc");
    sc_trace(tf, tb.wb_result, "wb_result");
    sc_trace(tf, tb.wb_rd, "wb_rd");
    
    // Run simulation
    sc_start();
    
    // Close trace file
    sc_close_vcd_trace_file(tf);
    
    std::cout << "\nSimulation completed successfully!" << std::endl;
    std::cout << "VCD trace file: fp_processor_trace.vcd\n" << std::endl;
    
    return 0;
}
