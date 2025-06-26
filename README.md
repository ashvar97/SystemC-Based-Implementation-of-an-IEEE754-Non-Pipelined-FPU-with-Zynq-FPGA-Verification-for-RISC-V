IEEE 754 Pipelined Floating-Point Unit with SystemC and FPGA Implementation
A comprehensive implementation of an IEEE 754-compliant pipelined floating-point processor using SystemC modeling, high-level synthesis, and FPGA verification on Xilinx Zynq platform.
📋 Overview
This project presents a complete design flow for implementing a floating-point processor that supports IEEE 754 standard arithmetic operations (addition, subtraction, multiplication, division) within a five-stage pipeline architecture. The implementation leverages SystemC for high-level modeling and Intel Compiler for SystemC (ICSC) for RTL generation, with final deployment on Xilinx Zynq FPGA.
🎯 Key Features

IEEE 754 Compliance: Full support for single-precision (32-bit) floating-point format
Pipelined Architecture: 5-stage pipeline (Fetch, Decode, Execute, Memory, Writeback)
Complete Arithmetic Suite: Addition, subtraction, multiplication, and division operations
Special Case Handling: Proper handling of zero, infinity, NaN, and denormalized numbers
High-Level Synthesis: SystemC to SystemVerilog translation using ICSC
FPGA Implementation: Verified on Xilinx Zynq XC7Z020 platform
Comprehensive Verification: Cross-platform verification with RISC-V Spike simulator

🏗️ Architecture
Pipeline Stages

Instruction Fetch (IF): Program counter management and instruction memory access
Decode (ID): Instruction decoding and register file access
Execute (EX): IEEE 754 floating-point arithmetic operations
Memory (MEM): Pipeline register stage
Writeback (WB): Result integration into processor state

Arithmetic Units

IEEE 754 Adder/Subtractor: 3-stage implementation with proper alignment and normalization
IEEE 754 Multiplier: 5-stage implementation with 24×24-bit significand multiplication
IEEE 754 Divider: Iterative division algorithm with restoring division
Exception Handling: Complete IEEE 754 exception detection and management

🛠️ Development Flow
mermaidgraph TD
    A[SystemC Modeling] --> B[Intel SystemC Compiler]
    B --> C[SystemVerilog RTL]
    C --> D[Vivado Synthesis]
    D --> E[FPGA Implementation]
    E --> F[Hardware Verification]
📁 Project Structure
├── src/
│   ├── systemc/           # SystemC source files
│   ├── generated/         # Generated SystemVerilog files
│   └── constraints/       # FPGA timing constraints
├── verification/
│   ├── testbench/         # Verification testbenches
│   └── results/           # Verification results
├── fpga/
│   ├── vivado_project/    # Vivado project files
│   └── bitstream/         # Generated bitstreams
└── docs/                  # Documentation and thesis
🚀 Getting Started
Prerequisites

SystemC Library: Version 2.3.3 or higher
Intel Compiler for SystemC (ICSC): Version 1.6.13
Xilinx Vivado: 2019.2 or compatible
GCC: 9.3.0 with C++14 support
CMake: 3.16 or higher

Building the Project

Clone the repository:
bashgit clone https://github.com/yourusername/ieee754-fpga-processor.git
cd ieee754-fpga-processor

Set up SystemC environment:
bashexport SYSTEMC_HOME=/path/to/systemc
export LD_LIBRARY_PATH=$SYSTEMC_HOME/lib-linux64:$LD_LIBRARY_PATH

Build SystemC simulation:
bashmkdir build && cd build
cmake ..
make

Generate SystemVerilog using ICSC:
bashcd $ICSC_HOME
source setenv.sh
cd designs/fp_processor
make fp_processor_sctool

Run FPGA implementation:
bashvivado -mode tcl -source implement_fpga.tcl


🧪 Verification
The project includes comprehensive verification methodology:

Functional Verification: SystemC testbenches with IEEE 754 test vectors
Cross-Verification: Comparison with RISC-V Spike simulator
Waveform Analysis: Detailed signal analysis using GTKWave
FPGA Verification: Hardware-in-the-loop testing on Zynq platform

Running Verification
bash# SystemC simulation
./fp_processor_sim

# Generate test vectors
./generate_test_vectors

# Cross-verify with Spike
spike --isa=rv32f test_program.elf
📊 Performance Results

Resource Utilization: 3,288 slice LUTs (6.18% of XC7Z020)
Operating Frequency: Up to 125 MHz
Pipeline Throughput: 1 operation per clock cycle (after pipeline fill)
Verification Coverage: 100% IEEE 754 special cases and edge conditions

🔧 Configuration
SystemC Compilation Options
cmakeset(CMAKE_CXX_STANDARD 14)
target_compile_definitions(fp_processor PRIVATE SC_INCLUDE_FX)
target_link_libraries(fp_processor systemc)
FPGA Constraints
tcl# Clock constraint (125 MHz)
create_clock -period 8.000 -name sys_clk_pin [get_ports clk]

# I/O constraints
set_property PACKAGE_PIN K17 [get_ports clk]
set_property IOSTANDARD LVCMOS33 [get_ports clk]
📚 Documentation

Thesis Document: Complete implementation details in docs/thesis.pdf
API Reference: SystemC module documentation
Design Specification: IEEE 754 compliance requirements
User Guide: Step-by-step implementation instructions

🤝 Contributing
Contributions are welcome! Please read our contributing guidelines:

Fork the repository
Create a feature branch (git checkout -b feature/amazing-feature)
Commit your changes (git commit -m 'Add amazing feature')
Push to the branch (git push origin feature/amazing-feature)
Open a Pull Request

📄 License
This project is licensed under the MIT License - see the LICENSE file for details.
🏆 Acknowledgments

Friedrich-Alexander-Universität Erlangen-Nürnberg - Department of Computer Science
Prof. Dr.-Ing. Dietmar Fey - Thesis supervisor
Intel Corporation - SystemC Compiler tools
Xilinx Inc. - FPGA development tools

📧 Contact
Ashwin Varkey - ashwin.varkey@fau.de
Project Link: https://github.com/yourusername/ieee754-fpga-processor
🔗 Related Work

IEEE 754-2019 Standard
RISC-V ISA Specification
SystemC Standard
Intel SystemC Compiler


This project represents a Master's thesis in Computational Engineering completed at Friedrich-Alexander-Universität Erlangen-Nürnberg in June 2025.
