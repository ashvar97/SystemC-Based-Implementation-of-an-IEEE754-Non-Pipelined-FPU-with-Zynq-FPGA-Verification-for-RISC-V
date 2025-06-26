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

