# Pipelined Floating-Point Unit (FPU) in SystemC

A complete IEEE 754 single-precision floating-point processor implementation with a 4-stage pipeline architecture, written in synthesizable SystemC RTL.

## 🏗️ Architecture Overview

The FPU implements a classic 4-stage pipeline:

```
┌───────┐    ┌────────┐    ┌─────────┐    ┌───────────┐
│ FETCH │ -> │ DECODE │ -> │ EXECUTE │ -> │ WRITEBACK │
└───────┘    └────────┘    └─────────┘    └───────────┘
```

### Pipeline Stages

1. **Fetch**: Instruction memory access with 256-word ROM
2. **Decode**: Instruction parsing and register file access (32 FP registers)
3. **Execute**: IEEE 754 arithmetic operations with multi-cycle division
4. **Writeback**: Result commit and exception flag management

## 🧮 Supported Operations

| Opcode | Operation | Cycles | Description |
|--------|-----------|--------|-------------|
| 0x0    | FADD      | 3      | Floating-point addition |
| 0x1    | FSUB      | 3      | Floating-point subtraction |
| 0x2    | FMUL      | 3      | Floating-point multiplication |
| 0x3    | FDIV      | 24+    | Floating-point division (iterative) |

## ✨ Key Features

### IEEE 754 Compliance
- **Special Values**: NaN, ±Infinity, ±Zero handling
- **Denormalized Numbers**: Full subnormal arithmetic support
- **Exception Flags**: Invalid operation, overflow, underflow, divide-by-zero, inexact
- **Rounding**: Proper mantissa normalization and range handling

### Advanced Pipeline Features
- **Out-of-Order Division**: 4-slot division queue for overlapped execution
- **Stall Support**: Pipeline freeze capability for hazard management
- **Exception Propagation**: Comprehensive floating-point exception tracking

### Synthesis-Ready Design
- **Fixed Resources**: Bounded loops, static arrays, no dynamic allocation
- **RTL Coding Style**: Clocked processes, explicit state machines
- **No Behavioral Code**: Pure structural/RTL implementation

### Register File
- 32 single-precision FP registers (f0-f31)
- f0 typically hardwired to zero (software convention)
- Direct bit-level access for testbench setup

## 🔧 Design Rationales

### Multi-Cycle Division
Division uses restoring algorithm with 24-cycle latency:
- **Throughput**: Up to 4 overlapped divisions via queue
- **Area**: Bounded resource usage for synthesis
- **Accuracy**: Bit-accurate IEEE 754 results

### Pipeline Depth
3-stage execution pipeline balances:
- **Latency**: Reasonable instruction completion time
- **Throughput**: One result per cycle (except division backpressure)
- **Complexity**: Manageable forwarding and hazard logic

## 🐛 Known Limitations

1. **No Hazard Detection**: RAW dependencies require software scheduling
2. **Fixed Instruction Set**: Only 4 operations currently supported
3. **Single-Precision Only**: No double-precision (64-bit) support
4. **Simplified Rounding**: Basic truncation, no IEEE rounding modes

## 🔬 Verification Approach

The testbench employs:
- **Directed Tests**: Known-answer verification
- **Corner Cases**: Boundary condition exploration  
- **Exception Validation**: Flag correctness checking
- **Timing Analysis**: Pipeline behavior verification

