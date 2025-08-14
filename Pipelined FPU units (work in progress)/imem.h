// Separate instruction memory module
SC_MODULE(InstructionMemory) {
    sc_in<sc_uint<32>> address;
    sc_out<sc_uint<32>> instruction;
    
    // Instruction memory array
    sc_signal<sc_uint<32>> imem[256];
    
    void process_read() {
        // Read from instruction memory based on address (word-aligned)
        instruction.write(imem[address.read().range(9,2)].read());
    }
    
    // No initialization - will be done by testbench
    
    SC_CTOR(InstructionMemory) {
        SC_METHOD(process_read);
        sensitive << address;
        for (int i = 0; i < 256; i++) {
            sensitive << imem[i];
        }
    }
};


SC_MODULE(InstructionMemory) {
    sc_in<sc_uint<32>> address;
    sc_out<sc_uint<32>> instruction;
    
    // Pre-loaded instruction memory for multiplication testing
    sc_uint<32> memory[32];
    
    void read_instruction() {
        sc_uint<32> addr = address.read();
        sc_uint<32> word_addr = addr >> 2;  // Convert byte address to word address
        
        if (word_addr < 32) {
            instruction.write(memory[word_addr]);
        } else {
            instruction.write(0);  // Return NOP for out-of-bounds access
        }
    }
    
    // Helper function to create FP instruction
    uint32_t createFPMulInstruction(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        uint32_t instruction = 0;
        instruction |= (0x08 << 25);      // funct7 = 8 for FMUL.S
        instruction |= (rs2 & 0x1F) << 20; // rs2
        instruction |= (rs1 & 0x1F) << 15; // rs1
        instruction |= (0x0 << 12);        // funct3 = 0 for single precision
        instruction |= (rd & 0x1F) << 7;   // rd
        instruction |= 0x53;               // opcode = 0x53 for FP operations
        return instruction;
    }
    
    SC_CTOR(InstructionMemory) {
        // Initialize instruction memory with multiplication test program
        // Using helper function for correct encoding
        
        // Test 1: fmul.s f3, f1, f2 (Pi * e)
        memory[0] = createFPMulInstruction(3, 1, 2);   // 0x102081D3
        
        // Test 2: fmul.s f6, f4, f5 (2.0 * 3.0)  
        memory[1] = createFPMulInstruction(6, 4, 5);   // 0x10520353
        
        // Test 3: fmul.s f9, f7, f8 (-1.5 * 4.0)
        memory[2] = createFPMulInstruction(9, 7, 8);   // 0x108384D3
        
        // Test 4: fmul.s f12, f10, f11 (0.5 * 0.25)
        memory[3] = createFPMulInstruction(12, 10, 11); // 0x10B50653
        
        // Test 5: fmul.s f15, f1, f4 (Pi * 2.0)
        memory[4] = createFPMulInstruction(15, 1, 4);   // 0x104087D3
        
        // Test 6: fmul.s f16, f10, f10 (0.5 * 0.5)
        memory[5] = createFPMulInstruction(16, 10, 10); // 0x10A50853
        
        // Termination (NOP)
        memory[6] = 0x00000000;
        
        // Fill remaining memory with NOPs
        for (int i = 7; i < 32; i++) {
            memory[i] = 0x00000000;
        }
        
        SC_METHOD(read_instruction);
        sensitive << address;
        
        cout << "IMEM: Initialized with corrected multiplication test program:" << endl;
        cout << "  [0] fmul.s f3, f1, f2   (0x" << hex << memory[0] << ") - Pi * e" << dec << endl;
        cout << "  [1] fmul.s f6, f4, f5   (0x" << hex << memory[1] << ") - 2.0 * 3.0" << dec << endl;
        cout << "  [2] fmul.s f9, f7, f8   (0x" << hex << memory[2] << ") - (-1.5) * 4.0" << dec << endl;
        cout << "  [3] fmul.s f12, f10, f11 (0x" << hex << memory[3] << ") - 0.5 * 0.25" << dec << endl;
        cout << "  [4] fmul.s f15, f1, f4  (0x" << hex << memory[4] << ") - Pi * 2.0" << dec << endl;
        cout << "  [5] fmul.s f16, f10, f10 (0x" << hex << memory[5] << ") - 0.5 * 0.5" << dec << endl;
        cout << "  [6] NOP (0x00000000) - termination" << endl;
        
        cout << "\nExpected Results:" << endl;
        cout << "  f3 = 8.53973 (Pi * e)" << endl;
        cout << "  f6 = 6.0 (2.0 * 3.0)" << endl;
        cout << "  f9 = -6.0 (-1.5 * 4.0)" << endl;
        cout << "  f12 = 0.125 (0.5 * 0.25)" << endl;
        cout << "  f15 = 6.28318 (Pi * 2.0)" << endl;
        cout << "  f16 = 0.25 (0.5 * 0.5)" << endl;
    }
};
