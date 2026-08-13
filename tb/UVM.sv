////////////////////////////////////////////////////////////////////////////////
// COMPLETE UVM Testbench for FP Pipelined Processor - FIXED VERSION
////////////////////////////////////////////////////////////////////////////////

`include "uvm_macros.svh"
import uvm_pkg::*;

////////////////////////////////////////////////////////////////////////////////
// Interface Definition - MUST BE BEFORE top module
////////////////////////////////////////////////////////////////////////////////
interface fp_interface(input logic clk);
  logic reset;
  logic stall;
  logic monitor_valid;
  logic [7:0] monitor_pc;
  logic [31:0] wb_result;
  logic [4:0] wb_rd;
endinterface

////////////////////////////////////////////////////////////////////////////////
// Transaction Class
////////////////////////////////////////////////////////////////////////////////
class fp_transaction extends uvm_sequence_item;
  rand bit [31:0] instruction;
  rand bit stall;
  bit [7:0] monitor_pc;
  bit monitor_valid;
  bit [31:0] result;
  bit [4:0] rd;
  
  `uvm_object_utils_begin(fp_transaction)
    `uvm_field_int(instruction, UVM_ALL_ON)
    `uvm_field_int(stall, UVM_ALL_ON)
    `uvm_field_int(monitor_pc, UVM_ALL_ON)
    `uvm_field_int(monitor_valid, UVM_ALL_ON)
    `uvm_field_int(result, UVM_ALL_ON)
    `uvm_field_int(rd, UVM_ALL_ON)
  `uvm_object_utils_end
  
  constraint stall_dist { stall dist {0 := 90, 1 := 10}; }
  
  function new(string name = "fp_transaction");
    super.new(name);
  endfunction
endclass

////////////////////////////////////////////////////////////////////////////////
// Sequencer
////////////////////////////////////////////////////////////////////////////////
class fp_sequencer extends uvm_sequencer #(fp_transaction);
  `uvm_component_utils(fp_sequencer)
  
  function new(string name = "fp_sequencer", uvm_component parent = null);
    super.new(name, parent);
  endfunction
endclass

////////////////////////////////////////////////////////////////////////////////
// Driver
////////////////////////////////////////////////////////////////////////////////
class fp_driver extends uvm_driver #(fp_transaction);
  `uvm_component_utils(fp_driver)
  
  virtual fp_interface vif;
  
  function new(string name = "fp_driver", uvm_component parent = null);
    super.new(name, parent);
  endfunction
  
  virtual function void build_phase(uvm_phase phase);
    super.build_phase(phase);
    if(!uvm_config_db#(virtual fp_interface)::get(this, "", "vif", vif))
      `uvm_fatal("NOVIF", "Virtual interface not found")
  endfunction
  
  virtual task run_phase(uvm_phase phase);
    fp_transaction tx;
    
    vif.reset = 1;
    vif.stall = 0;
    repeat(5) @(posedge vif.clk);
    vif.reset = 0;
    
    forever begin
      seq_item_port.get_next_item(tx);
      drive_transaction(tx);
      seq_item_port.item_done();
    end
  endtask
  
  virtual task drive_transaction(fp_transaction tx);
    vif.stall = tx.stall;
    @(posedge vif.clk);
  endtask
endclass

////////////////////////////////////////////////////////////////////////////////
// Monitor
////////////////////////////////////////////////////////////////////////////////
class fp_monitor extends uvm_monitor;
  `uvm_component_utils(fp_monitor)
  
  virtual fp_interface vif;
  uvm_analysis_port #(fp_transaction) mon_ap;
  
  function new(string name = "fp_monitor", uvm_component parent = null);
    super.new(name, parent);
  endfunction
  
  virtual function void build_phase(uvm_phase phase);
    super.build_phase(phase);
    if(!uvm_config_db#(virtual fp_interface)::get(this, "", "vif", vif))
      `uvm_fatal("NOVIF", "Virtual interface not found")
    mon_ap = new("mon_ap", this);
  endfunction
  
  virtual task run_phase(uvm_phase phase);
    fp_transaction tx;
    
    forever begin
      @(posedge vif.clk);
      if(!vif.reset) begin
        tx = fp_transaction::type_id::create("tx");
        tx.monitor_valid = vif.monitor_valid;
        tx.monitor_pc = vif.monitor_pc;
        tx.stall = vif.stall;
        tx.result = vif.wb_result;
        tx.rd = vif.wb_rd;
        mon_ap.write(tx);
      end
    end
  endtask
endclass

////////////////////////////////////////////////////////////////////////////////
// Agent
////////////////////////////////////////////////////////////////////////////////
class fp_agent extends uvm_agent;
  `uvm_component_utils(fp_agent)
  
  fp_driver driver;
  fp_sequencer sequencer;
  fp_monitor monitor;
  
  function new(string name = "fp_agent", uvm_component parent = null);
    super.new(name, parent);
  endfunction
  
  virtual function void build_phase(uvm_phase phase);
    super.build_phase(phase);
    monitor = fp_monitor::type_id::create("monitor", this);
    if(get_is_active() == UVM_ACTIVE) begin
      driver = fp_driver::type_id::create("driver", this);
      sequencer = fp_sequencer::type_id::create("sequencer", this);
    end
  endfunction
  
  virtual function void connect_phase(uvm_phase phase);
    if(get_is_active() == UVM_ACTIVE) begin
      driver.seq_item_port.connect(sequencer.seq_item_export);
    end
  endfunction
endclass

////////////////////////////////////////////////////////////////////////////////
// Coverage Collector
////////////////////////////////////////////////////////////////////////////////
class fp_coverage extends uvm_subscriber #(fp_transaction);
  `uvm_component_utils(fp_coverage)
  
  fp_transaction tx;
  real ipc;
  int total_cycles;
  int valid_instructions;
  int stall_cycles;
  
  covergroup cg_operations;
    pc_coverage: coverpoint tx.monitor_pc {
      bins low_pc[] = {[0:15]};
      bins mid_pc[] = {[16:31]};
      bins high_pc[] = {[32:63]};
    }
    
    stall_coverage: coverpoint tx.stall {
      bins no_stall = {0};
      bins stall = {1};
    }
    
    valid_coverage: coverpoint tx.monitor_valid {
      bins valid = {1};
      bins bubble = {0};
    }
  endgroup
  
  function new(string name = "fp_coverage", uvm_component parent = null);
    super.new(name, parent);
    cg_operations = new();
  endfunction
  
  virtual function void write(fp_transaction t);
    tx = t;
    cg_operations.sample();
    
    total_cycles++;
    if(tx.monitor_valid) valid_instructions++;
    if(tx.stall) stall_cycles++;
  endfunction
  
  virtual function void report_phase(uvm_phase phase);
    ipc = (total_cycles > 0) ? real'(valid_instructions) / real'(total_cycles) : 0.0;
    
    `uvm_info("COVERAGE", "="*60, UVM_LOW)
    `uvm_info("COVERAGE", "PERFORMANCE METRICS", UVM_LOW)
    `uvm_info("COVERAGE", "="*60, UVM_LOW)
    `uvm_info("COVERAGE", $sformatf("Total Cycles:          %0d", total_cycles), UVM_LOW)
    `uvm_info("COVERAGE", $sformatf("Valid Instructions:    %0d", valid_instructions), UVM_LOW)
    `uvm_info("COVERAGE", $sformatf("Stall Cycles:          %0d", stall_cycles), UVM_LOW)
    `uvm_info("COVERAGE", $sformatf("IPC (Instr/Cycle):     %0.3f", ipc), UVM_LOW)
    `uvm_info("COVERAGE", $sformatf("Functional Coverage:   %0.2f%%", cg_operations.get_coverage()), UVM_LOW)
    `uvm_info("COVERAGE", "="*60, UVM_LOW)
  endfunction
endclass

////////////////////////////////////////////////////////////////////////////////
// Scoreboard
////////////////////////////////////////////////////////////////////////////////
class fp_scoreboard extends uvm_scoreboard;
  `uvm_component_utils(fp_scoreboard)
  
  uvm_analysis_imp #(fp_transaction, fp_scoreboard) sb_imp;
  int instruction_count = 0;
  int valid_count = 0;
  bit [31:0] expected_results[256];
  bit [7:0] last_pc[$];
  
  function new(string name = "fp_scoreboard", uvm_component parent = null);
    super.new(name, parent);
  endfunction
  
  virtual function void build_phase(uvm_phase phase);
    super.build_phase(phase);
    sb_imp = new("sb_imp", this);
    calculate_expected_results();
  endfunction
  
  virtual function void calculate_expected_results();
    `uvm_info("SCOREBOARD", "Calculating expected results...", UVM_HIGH)
  endfunction
  
  function real fp32_to_real(bit [31:0] fp);
    int sign, exponent;
    real mantissa, result;
    
    sign = fp[31];
    exponent = fp[30:23];
    mantissa = 1.0;
    
    for(int i = 0; i < 23; i++) begin
      if(fp[22-i]) mantissa += (2.0 ** (-(i+1)));
    end
    
    if(exponent == 0 && fp[22:0] == 0)
      return 0.0;
    
    result = mantissa * (2.0 ** (exponent - 127));
    return sign ? -result : result;
  endfunction
  
  function bit check_fp_result(bit [31:0] result, bit [31:0] expected, real tolerance = 0.001);
    real r_result, r_expected, diff;
    
    r_result = fp32_to_real(result);
    r_expected = fp32_to_real(expected);
    diff = (r_result > r_expected) ? (r_result - r_expected) : (r_expected - r_result);
    
    return (diff < tolerance);
  endfunction
  
  virtual function void write(fp_transaction tx);
    string result_str;
    
    if(tx.monitor_valid) begin
      valid_count++;
      
      if(last_pc.size() > 0 && last_pc[$] == tx.monitor_pc) begin
        `uvm_warning("SCOREBOARD", $sformatf("Duplicate PC detected: 0x%0h", tx.monitor_pc))
      end
      last_pc.push_back(tx.monitor_pc);
      
      result_str = $sformatf("PC=0x%02h | Rd=x%0d | Result=0x%08h | FP=%0.6f", 
                             tx.monitor_pc, tx.rd, tx.result, fp32_to_real(tx.result));
      `uvm_info("SCOREBOARD", result_str, UVM_MEDIUM)
    end
    instruction_count++;
  endfunction
  
  virtual function void report_phase(uvm_phase phase);
    `uvm_info("SCOREBOARD", "="*60, UVM_LOW)
    `uvm_info("SCOREBOARD", "FINAL RESULTS", UVM_LOW)
    `uvm_info("SCOREBOARD", "="*60, UVM_LOW)
    `uvm_info("SCOREBOARD", $sformatf("Total cycles monitored:        %0d", instruction_count), UVM_LOW)
    `uvm_info("SCOREBOARD", $sformatf("Valid instructions completed:  %0d", valid_count), UVM_LOW)
    
    if(last_pc.size() > 0) begin
      `uvm_info("SCOREBOARD", $sformatf("Unique PCs executed:           %0d", last_pc.size()), UVM_LOW)
      if(last_pc.size() != valid_count)
        `uvm_warning("SCOREBOARD", "PC count mismatch - possible duplicate execution")
    end
    `uvm_info("SCOREBOARD", "="*60, UVM_LOW)
  endfunction
endclass

////////////////////////////////////////////////////////////////////////////////
// Environment
////////////////////////////////////////////////////////////////////////////////
class fp_env extends uvm_env;
  `uvm_component_utils(fp_env)
  
  fp_agent agent;
  fp_scoreboard scoreboard;
  fp_coverage coverage;
  
  function new(string name = "fp_env", uvm_component parent = null);
    super.new(name, parent);
  endfunction
  
  virtual function void build_phase(uvm_phase phase);
    super.build_phase(phase);
    agent = fp_agent::type_id::create("agent", this);
    scoreboard = fp_scoreboard::type_id::create("scoreboard", this);
    coverage = fp_coverage::type_id::create("coverage", this);
  endfunction
  
  virtual function void connect_phase(uvm_phase phase);
    agent.monitor.mon_ap.connect(scoreboard.sb_imp);
    agent.monitor.mon_ap.connect(coverage.analysis_export);
  endfunction
endclass

////////////////////////////////////////////////////////////////////////////////
// Sequences
////////////////////////////////////////////////////////////////////////////////
class fp_base_sequence extends uvm_sequence #(fp_transaction);
  `uvm_object_utils(fp_base_sequence)
  
  function new(string name = "fp_base_sequence");
    super.new(name);
  endfunction
  
  virtual task body();
    fp_transaction tx;
    
    repeat(100) begin
      tx = fp_transaction::type_id::create("tx");
      start_item(tx);
      assert(tx.randomize());
      finish_item(tx);
    end
  endtask
endclass

class fp_simple_sequence extends uvm_sequence #(fp_transaction);
  `uvm_object_utils(fp_simple_sequence)
  
  function new(string name = "fp_simple_sequence");
    super.new(name);
  endfunction
  
  virtual task body();
    fp_transaction tx;
    
    repeat(50) begin
      tx = fp_transaction::type_id::create("tx");
      start_item(tx);
      assert(tx.randomize() with {stall == 0;});
      finish_item(tx);
    end
  endtask
endclass

////////////////////////////////////////////////////////////////////////////////
// Tests
////////////////////////////////////////////////////////////////////////////////
class fp_test_base extends uvm_test;
  `uvm_component_utils(fp_test_base)
  
  fp_env env;
  
  function new(string name = "fp_test_base", uvm_component parent = null);
    super.new(name, parent);
  endfunction
  
  virtual function void build_phase(uvm_phase phase);
    super.build_phase(phase);
    env = fp_env::type_id::create("env", this);
  endfunction
  
  virtual function void end_of_elaboration_phase(uvm_phase phase);
    uvm_top.print_topology();
  endfunction
  
  virtual task run_phase(uvm_phase phase);
    phase.raise_objection(this);
    #1000ns;
    phase.drop_objection(this);
  endtask
endclass

class fp_simple_test extends fp_test_base;
  `uvm_component_utils(fp_simple_test)
  
  function new(string name = "fp_simple_test", uvm_component parent = null);
    super.new(name, parent);
  endfunction
  
  virtual task run_phase(uvm_phase phase);
    fp_simple_sequence seq;
    
    phase.raise_objection(this);
    
    `uvm_info("TEST", "Starting Simple Test - No Stalls", UVM_LOW)
    seq = fp_simple_sequence::type_id::create("seq");
    seq.start(env.agent.sequencer);
    
    #2000ns;
    
    phase.drop_objection(this);
  endtask
endclass

////////////////////////////////////////////////////////////////////////////////
// Top Module
////////////////////////////////////////////////////////////////////////////////
module top;
  logic clk;
  
  // Clock generation
  initial begin
    clk = 0;
    forever #5 clk = ~clk;
  end
  
  // Interface instance
  fp_interface vif(clk);
  
  // DUT instantiation
  FPPipelinedProcessor dut (
    .clk(vif.clk),
    .reset(vif.reset),
    .stall(vif.stall),
    .monitor_valid(vif.monitor_valid),
    .monitor_pc(vif.monitor_pc)
  );
  
  // Connect writeback signals
  assign vif.wb_result = dut.wb_result_out;
  assign vif.wb_rd = dut.wb_rd_out;
  
  // Initialize instruction memory - FIXED ENCODING
  initial begin
    // Initialize all memory to zero
    for(int i = 0; i < 256; i++) begin
      dut.imem.imem[i] = 32'h00000000;
    end
    
    // FIXED: Write to x6-x13 instead of x0
    // Test 1: x6 = x1 + x2 (1.0 + 2.0 = 3.0)
    dut.imem.imem[0] = 32'b0000000_00010_00001_000_00110_0110011;
    
    // Test 2: x7 = x2 + x4 (2.0 + 0.5 = 2.5)
    dut.imem.imem[1] = 32'b0000000_00100_00010_000_00111_0110011;
    
    // Test 3: x8 = x2 - x3 (2.0 - 3.0 = -1.0)
    dut.imem.imem[2] = 32'b0000100_00011_00010_000_01000_0110011;
    
    // Test 4: x9 = x1 * x2 (1.0 * 2.0 = 2.0)
    dut.imem.imem[3] = 32'b0001000_00010_00001_000_01001_0110011;
    
    // Test 5: x10 = x4 / x3 (0.5 / 3.0 = 0.166...)
    dut.imem.imem[4] = 32'b0001100_00011_00100_000_01010_0110011;
    
    // Test 6: x11 = x5 + x1 (4.0 + 1.0 = 5.0)
    dut.imem.imem[5] = 32'b0000000_00001_00101_000_01011_0110011;
    
    // Test 7: x12 = x2 * x3 (2.0 * 3.0 = 6.0)
    dut.imem.imem[6] = 32'b0001000_00011_00010_000_01100_0110011;
    
    // Test 8: x13 = x5 - x1 (4.0 - 1.0 = 3.0)
    dut.imem.imem[7] = 32'b0000100_00001_00101_000_01101_0110011;
    
    // End marker
    dut.imem.imem[8] = 32'h00000000;
    
    // Initialize registers
    dut.reg_file[0] = 32'h00000000; // 0.0
    dut.reg_file[1] = 32'h3F800000; // 1.0
    dut.reg_file[2] = 32'h40000000; // 2.0
    dut.reg_file[3] = 32'h40400000; // 3.0
    dut.reg_file[4] = 32'h3F000000; // 0.5
    dut.reg_file[5] = 32'h40800000; // 4.0
    dut.reg_file[6] = 32'hBF800000; // -1.0
    dut.reg_file[7] = 32'h3DCCCCCD; // 0.1
    
    $display("\n========================================");
    $display("EXPECTED RESULTS:");
    $display("========================================");
    $display("PC=0x00: x6  = 1.0 + 2.0 = 3.0");
    $display("PC=0x04: x7  = 2.0 + 0.5 = 2.5");
    $display("PC=0x08: x8  = 2.0 - 3.0 = -1.0");
    $display("PC=0x0c: x9  = 1.0 * 2.0 = 2.0");
    $display("PC=0x10: x10 = 0.5 / 3.0 = 0.166...");
    $display("PC=0x14: x11 = 4.0 + 1.0 = 5.0");
    $display("PC=0x18: x12 = 2.0 * 3.0 = 6.0");
    $display("PC=0x1c: x13 = 4.0 - 1.0 = 3.0");
    $display("========================================\n");
    
    `uvm_info("TOP", "Initialized instruction memory and registers", UVM_MEDIUM)
  end
  
  // UVM config and run
  initial begin
    uvm_config_db#(virtual fp_interface)::set(null, "*", "vif", vif);
    run_test("fp_simple_test");
  end
  
  // Waveform dump
  initial begin
    $dumpfile("dump.vcd");
    $dumpvars(0, top);
  end
  
  // Timeout watchdog
  initial begin
    #50000ns;
    `uvm_fatal("TIMEOUT", "Test timeout!")
    $finish;
  end
  
  // Performance monitor
  int cycle_count = 0;
  int valid_count = 0;
  
  always @(posedge clk) begin
    if(!vif.reset) begin
      cycle_count++;
      if(vif.monitor_valid)
        valid_count++;
    end
  end
  
  final begin
    real ipc;
    ipc = (cycle_count > 0) ? real'(valid_count) / real'(cycle_count) : 0.0;
    $display("\n=== Simulation Statistics ===");
    $display("Total Cycles:       %0d", cycle_count);
    $display("Valid Instructions: %0d", valid_count);
    $display("IPC:                %0.3f", ipc);
    $display("============================\n");
  end
endmodule
