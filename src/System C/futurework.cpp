// Final Optimized Testbench for FP Processor Performance Analysis
module fp_processor_tb;
  // Basic signals
  logic clk;
  logic reset;
  logic stall;
  logic monitor_valid;
  logic [7:0] monitor_pc;
  
  // Performance metrics
  integer cycle_count;
  integer instruction_count;
  integer pipeline_fill_cycles;
  integer total_latency;
  integer instruction_start_times[0:255];
  integer instruction_latencies[0:3]; // For ADD, SUB, MUL, DIV
  integer operation_counts[0:3];      // For ADD, SUB, MUL, DIV
  real avg_latency;
  real ipc;
  real steady_state_ipc;
  
  // Timestamps for accurate measurement
  integer first_instruction_time;
  integer last_instruction_time;
  
  // Test program with various instruction patterns
  logic [31:0] test_program[0:255];
  
  // For monitoring specific stages
  integer if_active_cycles;
  integer de_active_cycles;
  integer ex_active_cycles;
  integer mem_active_cycles;
  integer wb_active_cycles;
  
  // DUT instantiation  
  FPPipelinedProcessor dut (
    .clk(clk),
    .reset(reset),
    .stall(stall),
    .monitor_valid(monitor_valid),
    .monitor_pc(monitor_pc)
  );
  
  // Clock generation
  always begin
    #5 clk = ~clk;
  end
  
  // Monitor pipeline stages
  always @(posedge clk) begin
    if (!reset) begin
      // Track active cycles for each stage
      if (dut.ifu_valid_out) if_active_cycles = if_active_cycles + 1;
      if (dut.decode_valid_out) de_active_cycles = de_active_cycles + 1;
      if (dut.ex_valid_out) ex_active_cycles = ex_active_cycles + 1;
      if (dut.mem_valid_out) mem_active_cycles = mem_active_cycles + 1;
      if (dut.wb_valid_out) wb_active_cycles = wb_active_cycles + 1;
      
      // Track first and last instruction times
      if (monitor_valid && first_instruction_time == 0) begin
        first_instruction_time = cycle_count;
      end
      
      if (monitor_valid) begin
        last_instruction_time = cycle_count;
      end
    end
  end
  
  // Monitor valid signal and PC for instruction completion
  always @(posedge clk) begin
    if (monitor_valid) begin
      integer latency;
      integer pc_idx;
      
      // Convert PC to word index
      pc_idx = monitor_pc >> 2;
      
      // Calculate latency
      if (instruction_start_times[pc_idx] != 0) begin
        latency = cycle_count - instruction_start_times[pc_idx];
        total_latency = total_latency + latency;
        
        // Update operation-specific latencies based on opcode
        case (test_program[pc_idx] >> 25)
          0: begin // ADD
            instruction_latencies[0] = instruction_latencies[0] + latency;
            operation_counts[0] = operation_counts[0] + 1;
          end
          4: begin // SUB
            instruction_latencies[1] = instruction_latencies[1] + latency;
            operation_counts[1] = operation_counts[1] + 1;
          end
          8: begin // MUL
            instruction_latencies[2] = instruction_latencies[2] + latency;
            operation_counts[2] = operation_counts[2] + 1;
          end
          12: begin // DIV
            instruction_latencies[3] = instruction_latencies[3] + latency;
            operation_counts[3] = operation_counts[3] + 1;
          end
        endcase
      end
      
      instruction_count = instruction_count + 1;
      
      // Calculate metrics
      if (instruction_count > 0) begin
        avg_latency = real'(total_latency) / real'(instruction_count);
        ipc = real'(instruction_count) / real'(cycle_count);
        
        // Calculate steady-state IPC (after pipeline fill)
        if (first_instruction_time > 0) begin
          steady_state_ipc = real'(instruction_count - 1) / 
                              real'(last_instruction_time - first_instruction_time);
        end
      end
    end
    
    // Track when instructions enter the pipeline
    if (dut.ifu_valid_out && (dut.pc_out >> 2) < 255) begin
      integer pc_word_index;
      pc_word_index = dut.pc_out >> 2;
      
      if (instruction_start_times[pc_word_index] == 0) begin
        instruction_start_times[pc_word_index] = cycle_count;
      end
    end
    
    cycle_count = cycle_count + 1;
  end
  
  // Create different test programs
  task create_independent_program;
    integer i;
    integer addr;
    
    addr = 0;
    
    // ADD sequence (10 independent ADDs)
    for (i = 0; i < 10; i = i + 1) begin
      test_program[addr] = (0 << 25) | ((i+1) << 7) | ((i+2) << 15) | ((i+3) << 20);
      addr = addr + 1;
    end
    
    // SUB sequence (10 independent SUBs)
    for (i = 0; i < 10; i = i + 1) begin
      test_program[addr] = (4 << 25) | ((i+11) << 7) | ((i+12) << 15) | ((i+13) << 20);
      addr = addr + 1;
    end
    
    // MUL sequence (10 independent MULs)
    for (i = 0; i < 10; i = i + 1) begin
      test_program[addr] = (8 << 25) | ((i+21) << 7) | ((i+22) << 15) | ((i+23) << 20);
      addr = addr + 1;
    end
    
    // DIV sequence (10 independent DIVs)
    for (i = 0; i < 10; i = i + 1) begin
      test_program[addr] = (12 << 25) | ((i+31) << 7) | ((i+32) << 15) | ((i+33) << 20);
      addr = addr + 1;
    end
    
    // End program
    test_program[addr] = 0;
  endtask
  
  // Create dependent program (test data hazards)
  task create_dependent_program;
    integer i;
    integer addr;
    
    addr = 0;
    
    // Chain of dependent ADDs (r1 = r1 + r2)
    for (i = 0; i < 10; i = i + 1) begin
      test_program[addr] = (0 << 25) | (1 << 7) | (1 << 15) | (2 << 20);
      addr = addr + 1;
    end
    
    // Chain of dependent SUBs (r3 = r3 - r4)
    for (i = 0; i < 10; i = i + 1) begin
      test_program[addr] = (4 << 25) | (3 << 7) | (3 << 15) | (4 << 20);
      addr = addr + 1;
    end
    
    // Chain of dependent MULs (r5 = r5 * r6)
    for (i = 0; i < 10; i = i + 1) begin
      test_program[addr] = (8 << 25) | (5 << 7) | (5 << 15) | (6 << 20);
      addr = addr + 1;
    end
    
    // Chain of dependent DIVs (r7 = r7 / r8)
    for (i = 0; i < 10; i = i + 1) begin
      test_program[addr] = (12 << 25) | (7 << 7) | (7 << 15) | (8 << 20);
      addr = addr + 1;
    end
    
    // End program
    test_program[addr] = 0;
  endtask
  
  // Create mixed program (testing dependencies and different patterns)
  task create_mixed_program;
    integer i;
    integer addr;
    
    addr = 0;
    
    // Pattern with RAW hazards
    test_program[addr++] = (0 << 25) | (1 << 7) | (2 << 15) | (3 << 20);  // r1 = r2 + r3
    test_program[addr++] = (0 << 25) | (4 << 7) | (1 << 15) | (5 << 20);  // r4 = r1 + r5 (RAW)
    test_program[addr++] = (4 << 25) | (6 << 7) | (4 << 15) | (7 << 20);  // r6 = r4 - r7 (RAW)
    test_program[addr++] = (8 << 25) | (8 << 7) | (6 << 15) | (9 << 20);  // r8 = r6 * r9 (RAW)
    test_program[addr++] = (12 << 25) | (10 << 7) | (11 << 15) | (12 << 20); // r10 = r11 / r12 (no hazard)
    
    // Pattern with no hazards
    test_program[addr++] = (0 << 25) | (13 << 7) | (14 << 15) | (15 << 20); // r13 = r14 + r15
    test_program[addr++] = (4 << 25) | (16 << 7) | (17 << 15) | (18 << 20); // r16 = r17 - r18
    test_program[addr++] = (8 << 25) | (19 << 7) | (20 << 15) | (21 << 20); // r19 = r20 * r21
    test_program[addr++] = (12 << 25) | (22 << 7) | (23 << 15) | (24 << 20); // r22 = r23 / r24
    
    // Pattern with WAW hazards
    test_program[addr++] = (0 << 25) | (25 << 7) | (26 << 15) | (27 << 20); // r25 = r26 + r27
    test_program[addr++] = (4 << 25) | (25 << 7) | (28 << 15) | (29 << 20); // r25 = r28 - r29 (WAW)
    
    // End program
    test_program[addr] = 0;
  endtask
  
  // Run a test and report results
  task run_test;
    input string test_name;
    
    // Reset metrics
    cycle_count = 0;
    instruction_count = 0;
    total_latency = 0;
    avg_latency = 0;
    ipc = 0;
    steady_state_ipc = 0;
    first_instruction_time = 0;
    last_instruction_time = 0;
    if_active_cycles = 0;
    de_active_cycles = 0;
    ex_active_cycles = 0;
    mem_active_cycles = 0;
    wb_active_cycles = 0;
    
    for (integer i = 0; i < 4; i = i + 1) begin
      instruction_latencies[i] = 0;
      operation_counts[i] = 0;
    end
    
    for (integer i = 0; i < 256; i = i + 1) begin
      instruction_start_times[i] = 0;
    end
    
    // Reset processor
    reset = 1;
    repeat (5) @(posedge clk);
    reset = 0;
    
    // Load test program into instruction memory
    for (integer i = 0; i < 256; i = i + 1) begin
      dut.imem.imem[i] = test_program[i];
    end
    
    // Run until program terminates or timeout
    fork
      begin
        wait (dut.terminated == 1);
        repeat (5) @(posedge clk); // Let pipeline drain
        $display("Program terminated normally at cycle %0d", cycle_count);
      end
      begin
        repeat (1000) @(posedge clk);
        $display("Simulation timeout at cycle %0d", cycle_count);
      end
    join_any
    
    // Calculate pipeline fill time
    pipeline_fill_cycles = first_instruction_time;
    
    // Report results
    $display("\n============== %s Performance Report ==============", test_name);
    $display("Total cycles: %0d", cycle_count);
    $display("Instructions completed: %0d", instruction_count);
    $display("Pipeline fill time: %0d cycles", pipeline_fill_cycles);
    $display("Overall IPC: %f", ipc);
    $display("Steady-state IPC: %f", steady_state_ipc);
    $display("Average instruction latency: %f cycles", avg_latency);
    
    $display("\nOperation counts and mix:");
    $display("  ADD: %0d instructions (%0.1f%%)", operation_counts[0], 
             instruction_count > 0 ? 100.0*real'(operation_counts[0])/real'(instruction_count) : 0.0);
    $display("  SUB: %0d instructions (%0.1f%%)", operation_counts[1], 
             instruction_count > 0 ? 100.0*real'(operation_counts[1])/real'(instruction_count) : 0.0);
    $display("  MUL: %0d instructions (%0.1f%%)", operation_counts[2], 
             instruction_count > 0 ? 100.0*real'(operation_counts[2])/real'(instruction_count) : 0.0);
    $display("  DIV: %0d instructions (%0.1f%%)", operation_counts[3], 
             instruction_count > 0 ? 100.0*real'(operation_counts[3])/real'(instruction_count) : 0.0);
    
    $display("\nAverage latencies by operation:");
    if (operation_counts[0] > 0)
      $display("  ADD: %f cycles", real'(instruction_latencies[0]) / real'(operation_counts[0]));
    if (operation_counts[1] > 0)
      $display("  SUB: %f cycles", real'(instruction_latencies[1]) / real'(operation_counts[1]));
    if (operation_counts[2] > 0) 
      $display("  MUL: %f cycles", real'(instruction_latencies[2]) / real'(operation_counts[2]));
    if (operation_counts[3] > 0)
      $display("  DIV: %f cycles", real'(instruction_latencies[3]) / real'(operation_counts[3]));
    
    $display("\nPipeline utilization:");
    $display("  IF stage: %0.1f%% active", 100.0*real'(if_active_cycles)/real'(cycle_count));
    $display("  DE stage: %0.1f%% active", 100.0*real'(de_active_cycles)/real'(cycle_count));
    $display("  EX stage: %0.1f%% active", 100.0*real'(ex_active_cycles)/real'(cycle_count));
    $display("  MEM stage: %0.1f%% active", 100.0*real'(mem_active_cycles)/real'(cycle_count));
    $display("  WB stage: %0.1f%% active", 100.0*real'(wb_active_cycles)/real'(cycle_count));
    $display("===========================================================");
  endtask
  
  // Test with stalls
  task run_stall_test;
    input integer stall_rate; // Percentage (0-100)
    
    string test_name;
    $sformat(test_name, "Stall Test (%0d%% stall rate)", stall_rate);
    
    // Reset metrics
    cycle_count = 0;
    instruction_count = 0;
    total_latency = 0;
    avg_latency = 0;
    ipc = 0;
    steady_state_ipc = 0;
    first_instruction_time = 0;
    last_instruction_time = 0;
    if_active_cycles = 0;
    de_active_cycles = 0;
    ex_active_cycles = 0;
    mem_active_cycles = 0;
    wb_active_cycles = 0;
    
    for (integer i = 0; i < 4; i = i + 1) begin
      instruction_latencies[i] = 0;
      operation_counts[i] = 0;
    end
    
    for (integer i = 0; i < 256; i = i + 1) begin
      instruction_start_times[i] = 0;
    end
    
    // Reset processor
    reset = 1;
    stall = 0;
    repeat (5) @(posedge clk);
    reset = 0;
    
    // Create independent program
    create_independent_program();
    
    // Load test program into instruction memory
    for (integer i = 0; i < 256; i = i + 1) begin
      dut.imem.imem[i] = test_program[i];
    end
    
    // Run with stalls
    fork
      begin
        wait (dut.terminated == 1);
        repeat (5) @(posedge clk); // Let pipeline drain
        $display("Program terminated normally at cycle %0d", cycle_count);
      end
      begin
        repeat (1000) @(posedge clk);
        $display("Simulation timeout at cycle %0d", cycle_count);
      end
      begin
        repeat (1000) begin
          @(posedge clk);
          // Use random stalls based on specified rate
          stall = ($urandom_range(100) < stall_rate);
        end
      end
    join_any
    
    // Ensure stall is deasserted at end
    stall = 0;
    
    // Calculate pipeline fill time
    pipeline_fill_cycles = first_instruction_time;
    
    // Report results
    $display("\n============== %s Performance Report ==============", test_name);
    $display("Total cycles: %0d", cycle_count);
    $display("Instructions completed: %0d", instruction_count);
    $display("Pipeline fill time: %0d cycles", pipeline_fill_cycles);
    $display("Overall IPC: %f", ipc);
    $display("Steady-state IPC: %f", steady_state_ipc);
    $display("Average instruction latency: %f cycles", avg_latency);
    
    $display("\nOperation counts and mix:");
    $display("  ADD: %0d instructions (%0.1f%%)", operation_counts[0], 
             instruction_count > 0 ? 100.0*real'(operation_counts[0])/real'(instruction_count) : 0.0);
    $display("  SUB: %0d instructions (%0.1f%%)", operation_counts[1], 
             instruction_count > 0 ? 100.0*real'(operation_counts[1])/real'(instruction_count) : 0.0);
    $display("  MUL: %0d instructions (%0.1f%%)", operation_counts[2], 
             instruction_count > 0 ? 100.0*real'(operation_counts[2])/real'(instruction_count) : 0.0);
    $display("  DIV: %0d instructions (%0.1f%%)", operation_counts[3], 
             instruction_count > 0 ? 100.0*real'(operation_counts[3])/real'(instruction_count) : 0.0);
    
    $display("\nAverage latencies by operation:");
    if (operation_counts[0] > 0)
      $display("  ADD: %f cycles", real'(instruction_latencies[0]) / real'(operation_counts[0]));
    if (operation_counts[1] > 0)
      $display("  SUB: %f cycles", real'(instruction_latencies[1]) / real'(operation_counts[1]));
    if (operation_counts[2] > 0) 
      $display("  MUL: %f cycles", real'(instruction_latencies[2]) / real'(operation_counts[2]));
    if (operation_counts[3] > 0)
      $display("  DIV: %f cycles", real'(instruction_latencies[3]) / real'(operation_counts[3]));
    
    $display("\nPipeline utilization:");
    $display("  IF stage: %0.1f%% active", 100.0*real'(if_active_cycles)/real'(cycle_count));
    $display("  DE stage: %0.1f%% active", 100.0*real'(de_active_cycles)/real'(cycle_count));
    $display("  EX stage: %0.1f%% active", 100.0*real'(ex_active_cycles)/real'(cycle_count));
    $display("  MEM stage: %0.1f%% active", 100.0*real'(mem_active_cycles)/real'(cycle_count));
    $display("  WB stage: %0.1f%% active", 100.0*real'(wb_active_cycles)/real'(cycle_count));
    $display("===========================================================");
  endtask
  
  // Main test sequence
  initial begin
    // Initialize all variables
    clk = 0;
    reset = 1;
    stall = 0;
    cycle_count = 0;
    instruction_count = 0;
    total_latency = 0;
    avg_latency = 0;
    ipc = 0;
    steady_state_ipc = 0;
    first_instruction_time = 0;
    last_instruction_time = 0;
    if_active_cycles = 0;
    de_active_cycles = 0;
    ex_active_cycles = 0;
    mem_active_cycles = 0;
    wb_active_cycles = 0;
    pipeline_fill_cycles = 0;
    
    for (integer i = 0; i < 4; i = i + 1) begin
      instruction_latencies[i] = 0;
      operation_counts[i] = 0;
    end
    
    for (integer i = 0; i < 256; i = i + 1) begin
      instruction_start_times[i] = 0;
      test_program[i] = 0;
    end
    
    // Initialize register file with non-zero values
    for (integer i = 0; i < 32; i = i + 1) begin
      dut.reg_file[i] = 32'h3F800000; // IEEE-754 representation of 1.0
    end
    
    // Run test 1: Independent instructions
    create_independent_program();
    run_test("Independent Instructions");
    
    // Run test 2: Dependent instructions
    create_dependent_program();
    run_test("Dependent Instructions");
    
    // Run test 3: Mixed dependency pattern
    create_mixed_program();
    run_test("Mixed Dependency Pattern");
    
    // Run test 4: With stalls
    run_stall_test(10); // 10% stall rate
    
    // Run test 5: With higher stall rate
    run_stall_test(25); // 25% stall rate
    
    $display("\n============== Performance Summary ==============");
    $display("1. Independent instructions are the ideal case for this processor");
    $display("2. Dependent instructions show the impact of data hazards");
    $display("3. Mixed patterns represent real-world code sequences");
    $display("4. Stall tests show resilience to external delays");
    $display("5. This processor has a 5-stage pipeline");
    $display("   - Expected minimum latency: 5 cycles");
    $display("   - Maximum theoretical IPC: 1.0");
    $display("==================================================");
    
    $finish;
  end
  
endmodule









