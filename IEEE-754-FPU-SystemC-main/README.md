# IEEE 754 Floating-Point Unit using SystemC

## Overview

This project presents a **SystemC-based implementation** of an **IEEE 754 compliant Floating-Point Unit (FPU)**. The FPU supports fundamental arithmetic operations—**addition, subtraction, multiplication, and division**—for both **single-precision (32-bit)** floating-point numbers. Designed for hardware-accurate simulations, this module facilitates the integration and verification of floating-point arithmetic in digital systems.

## 🎯 Features

- **Arithmetic Operations**: Supports **addition**, **subtraction**, **multiplication**, and **division** operations
- **IEEE 754 Compliance**: Adheres to the IEEE 754 standard for binary floating-point arithmetic
- **Precision Support**: Implements both **single-precision (32-bit)** and **double-precision (64-bit)** formats
- **Exception Handling**: Detects and manages conditions such as **overflow**, **underflow**, **division by zero**, and **invalid operations**
- **Rounding Modes**: Incorporates multiple rounding modes, including **nearest-even**, **toward zero**, **toward positive infinity**, and **toward negative infinity**

## 📁 Repository Structure

```
.
├── .DS_Store                     # macOS directory metadata
├── .gitattributes                # Git configuration file
├── Addition_Final.cpp            # IEEE 754 floating-point addition implementation
├── Addition_check.cpp            # Testbench for addition operation verification
├── Division_Final.cpp            # Floating-point division implementation
├── Division_check.cpp            # Division operation test cases
├── Multiplication_Final.cpp      # Floating-point multiplier core
├── Multiplication_check.cpp      # Multiplier verification testbench
├── Subtract_Final.cpp            # Floating-point subtraction implementation
├── Verilog/                      # RTL implementations (if applicable)
└── README.md                     # Project documentation
```

## 🚀 Getting Started

### Prerequisites

- **SystemC Library**: Ensure the SystemC library is installed on your system. Refer to the [Accellera SystemC page](https://www.accellera.org/downloads/standards/systemc) for installation instructions
- **C++ Compiler**: GCC or compatible compiler with C++11 support

### Installation

1. **Install SystemC dependencies**:
   ```bash
   sudo apt install systemc-dev  # Ubuntu/Debian
   ```
   
   For other systems, download and install SystemC from the official website.

### Compilation and Execution

1. **Clone the Repository**:
   ```bash
   git clone https://github.com/ashvar97/IEEE-754-Floating-point-unit-using-System-C.git
   cd IEEE-754-Floating-point-unit-using-System-C
   ```

2. **Compile the Modules**:
   - Use a C++ compiler (e.g., `g++`) with SystemC includes and libraries
   - Example compilation command for addition module and its testbench:
     ```bash
     g++ -I /path/to/systemc/include -L /path/to/systemc/lib-linux Addition_Final.cpp Addition_check.cpp -o fp_addition -lsystemc -lm
     ```
   - Replace `/path/to/systemc/` with the actual SystemC installation path

3. **Run the Executable**:
   ```bash
   ./fp_addition
   ```
   
   Repeat the compilation and execution steps for multiplication, division, and subtraction modules by replacing the filenames accordingly.

### Quick Test Commands

```bash
# Compile and test addition
g++ -I $SYSTEMC_HOME/include -L $SYSTEMC_HOME/lib-linux64 Addition_Final.cpp Addition_check.cpp -o fp_addition -lsystemc -lm && ./fp_addition

# Compile and test multiplication  
g++ -I $SYSTEMC_HOME/include -L $SYSTEMC_HOME/lib-linux64 Multiplication_Final.cpp Multiplication_check.cpp -o fp_multiply -lsystemc -lm && ./fp_multiply

# Compile and test division
g++ -I $SYSTEMC_HOME/include -L $SYSTEMC_HOME/lib-linux64 Division_Final.cpp Division_check.cpp -o fp_divide -lsystemc -lm && ./fp_divide

# Compile and test subtraction
g++ -I $SYSTEMC_HOME/include -L $SYSTEMC_HOME/lib-linux64 Subtract_Final.cpp Subtract_check.cpp -o fp_subtract -lsystemc -lm && ./fp_subtract
```

## 💡 Usage

- **Integration**: Incorporate these modules into larger SystemC projects requiring floating-point arithmetic
- **Verification**: Utilize the provided testbenches (`*_check.cpp`) to verify the correctness of each arithmetic operation
- **Customization**: Modify the modules to support additional operations or to optimize performance for specific applications

## 🧪 Testing

Each arithmetic operation comes with its dedicated testbench:

- `Addition_check.cpp` - Tests floating-point addition
- `Multiplication_check.cpp` - Tests floating-point multiplication  
- `Division_check.cpp` - Tests floating-point division
- `Subtract_check.cpp` - Tests floating-point subtraction (assuming it exists)

The testbenches verify IEEE 754 compliance and handle edge cases including:
- Normal numbers
- Denormalized numbers
- Zero values
- Infinity values
- NaN (Not a Number) values

## 📄 Documentation

For detailed implementation specifics and SystemC modeling approaches, refer to the source code comments and the accompanying documentation.


## 👨‍💻 Author

**Ashwin Varkey**
- Email: [ashvar97@gmail.com](mailto:ashvar97@gmail.com)
- LinkedIn: [ashvar97](https://www.linkedin.com/in/ashvar97/)
- GitHub: [ashvar97](https://github.com/ashvar97)



## ⚠️ Disclaimer

*Note: This project is intended for educational purposes and may require further validation for deployment in production environments.*

## 🔗 Related Resources

- [IEEE 754-2019 Standard](https://ieeexplore.ieee.org/document/8766229)
- [SystemC Documentation](https://www.accellera.org/downloads/standards/systemc)
- [Floating-Point Arithmetic Principles](https://docs.oracle.com/cd/E19957-01/806-3568/ncg_goldberg.html)
