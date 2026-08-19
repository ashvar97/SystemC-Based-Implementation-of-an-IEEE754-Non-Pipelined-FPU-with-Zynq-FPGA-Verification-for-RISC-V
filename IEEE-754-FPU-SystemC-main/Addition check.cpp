#include <systemc.h>
#include <stdio.h>

// FloatingPointExtractor Module
SC_MODULE(FloatingPointExtractor) {
    sc_in<sc_uint<32>> a;
    sc_in<sc_uint<32>> b;
    sc_out<bool> a_sign;
    sc_out<sc_uint<8>> a_exp;
    sc_out<sc_uint<32>> a_significand;
    sc_out<bool> b_sign;
    sc_out<sc_uint<8>> b_exp;
    sc_out<sc_uint<32>> b_significand;
    sc_in<bool> clock;

    void extraction_process() {
        while (true) {
            wait();
            //Extraction
            bool a_sign0 = (a.read() & 0x80000000) >> 31;
            unsigned int a_exp0 = (a.read() & 0x7f800000) >> 23;
            unsigned int a_significand0 = (a.read() & 0x7fffff);

            bool b_sign0 = (b.read() & 0x80000000) >> 31;
            unsigned int b_exp0 = (b.read() & 0x7f800000) >> 23;
            unsigned int b_significand0 = (b.read() & 0x7fffff);

            unsigned int a_significand1 = (a_exp0 >= 1) ? (a_significand0 | (1 << 23)) : a_significand0;
            unsigned int b_significand1 = (b_exp0 >= 1) ? (b_significand0 | (1 << 23)) : b_significand0;

            unsigned int a_significand2 = (a_significand1 << 7);
            unsigned int b_significand2 = (b_significand1 << 7);

            unsigned int a_exp1 = ((a_exp0 == 0) ? 1 : a_exp0);
            unsigned int b_exp1 = ((b_exp0 == 0) ? 1 : b_exp0);
            //Special Cases
            if (a_exp0 == 255 && a_significand0 != 0) {
                a_sign.write(true);
                a_exp.write(0x7F);
                a_significand.write(0x7fffffff);
                b_sign.write(true);
                b_exp.write(0x7F);
                b_significand.write(0x7fffffff);
            } else if (b_exp0 == 255 && b_significand0 != 0) {
                a_sign.write(true);
                a_exp.write(0x7F);
                a_significand.write(0x7fffffff);
                b_sign.write(true);
                b_exp.write(0x7F);
                b_significand.write(0x7fffffff);
            } else if (a_exp0 == 255 && a_significand0 == 0 && b_exp0 == 255 && b_significand0 == 0 && a_sign0 != b_sign0) {
                a_sign.write(true);
                a_exp.write(0x7F);
                a_significand.write(0x7fffffff);
                b_sign.write(true);
                b_exp.write(0x7F);
                b_significand.write(0x7fffffff);
            } else if (a_exp0 == 255 && a_significand0 == 0) {
                a_sign.write(a_sign0);
                a_exp.write(a_exp0);
                a_significand.write(a_significand0);
                b_sign.write(true);
                b_exp.write(0x7F);
                b_significand.write(0x7fffffff);
            } else if (b_exp0 == 255 && b_significand0 == 0) {
                a_sign.write(true);
                a_exp.write(0x7F);
                a_significand.write(0x7fffffff);
                b_sign.write(b_sign0);
                b_exp.write(b_exp0);
                b_significand.write(b_significand0);
            } else {
    
                a_sign.write(a_sign0);
                a_exp.write(static_cast<sc_uint<8>>(a_exp1));
                a_significand.write(a_significand2);
                b_sign.write(b_sign0);
                b_exp.write(static_cast<sc_uint<8>>(b_exp1));
                b_significand.write(b_significand2);
            }
        }
    }
    //Constructor
    SC_CTOR(FloatingPointExtractor)
        : a("a"),
          b("b"),
          a_sign("a_sign"),
          a_exp("a_exp"),
          a_significand("a_significand"),
          b_sign("b_sign"),
          b_exp("b_exp"),
          b_significand("b_significand"),
          clock("clock") {
        SC_THREAD(extraction_process);
        sensitive << clock.pos();     //Clock signal
    }
};

// FloatingPointAdder Module
SC_MODULE(FloatingPointAdder) {
    sc_in<bool> a_sign;
    sc_in<sc_uint<8>> a_exp;
    sc_in<sc_uint<32>> a_significand;
    sc_in<bool> b_sign;
    sc_in<sc_uint<8>> b_exp;
    sc_in<sc_uint<32>> b_significand;
    sc_out<bool> result_sign;
    sc_out<sc_uint<8>> result_exp;
    sc_out<sc_uint<32>> result_significand;
    sc_in<bool> clock;

    void addition_process() {
        while (true) {
            wait();

            unsigned int ans_exp;
            unsigned int ans_significand;
            bool ans_sign;
            unsigned int a_significand3 = a_significand.read();
            unsigned int b_significand3 = b_significand.read();
        //Exponent Shifting
            if (a_exp.read() >= b_exp.read()) {
                unsigned int shift = a_exp.read() - b_exp.read();
                b_significand3 = (b_significand.read() >> ((shift > 31) ? 31 : shift));
                ans_exp = a_exp.read();
            } else {
                unsigned int shift = b_exp.read() - a_exp.read();
                a_significand3 = (a_significand.read() >> ((shift > 31) ? 31 : shift));
                ans_exp = b_exp.read();
            }
         //Significand shifting and adding
            if (a_sign.read() == b_sign.read()) {
                ans_significand = a_significand3 + b_significand3;
                ans_sign = a_sign.read();
            } else {
                if (a_significand3 > b_significand3) {
                    ans_sign = a_sign.read();
                    ans_significand = a_significand3 - b_significand3;
                } else if (a_significand3 < b_significand3) {
                    ans_sign = b_sign.read();
                    ans_significand = b_significand3 - a_significand3;
                } else if (a_significand3 == b_significand3) {
                    ans_sign = false;
                    ans_significand = a_significand3 - b_significand3;
                }
            }

            result_sign.write(ans_sign);
            result_exp.write(static_cast<sc_uint<8>>(ans_exp));
            result_significand.write(ans_significand);
        }
    }

    SC_CTOR(FloatingPointAdder)
        : a_sign("a_sign"),
          a_exp("a_exp"),
          a_significand("a_significand"),
          b_sign("b_sign"),
          b_exp("b_exp"),
          b_significand("b_significand"),
          result_sign("result_sign"),
          result_exp("result_exp"),
          result_significand("result_significand"),
          clock("clock") {
        SC_THREAD(addition_process);
        sensitive << clock.pos();
    }
};

SC_MODULE(FloatingPointNormaliser) {
    sc_in<bool> result_sign;
    sc_in<sc_uint<8>> result_exp;
    sc_in<sc_uint<32>> result_significand;
    sc_out<sc_uint<32>> nresult;
    sc_in<bool> clock;
    void normal_process() {
    
    
   int check=1,check1=0;
        while (true) {
            wait();

            unsigned int ans_exp = result_exp.read();
            unsigned int ans_significand = result_significand.read();
            bool ans_sign = result_sign.read();
    /* Normalization */
    int i;
    for (i=31; i>0 && ((ans_significand>>i) == 0); i-- ){;}
    
    if (i>23){

        //Rounding
        unsigned int twentyfourth = ((ans_significand&(1<<(i-23-1)))>>(i-23-1));

        unsigned int twentyfifth = 0;
        for(int j=0;j<i-23-1;j++){
            twentyfifth = twentyfifth | ((ans_significand & (1<<j))>>j);
        }

        if ((int(ans_exp) + (i-23) - 7) > 0 && (int(ans_exp) + (i-23) - 7) < 255){

            ans_significand = (ans_significand>>(i-23));

            ans_exp = ans_exp + (i-23) - 7;
            if (check == 2) {
                // Print binary representation of ans_significand in IEEE 754 format (25 bits)
                cout << "Binary rep. before rounding of significand(25 bits): ";
                for (int j = 24; j >= 0; j--) {
                    cout << ((ans_significand >> j) & 1);
                    if (j == 31 || j == 23|| j == 2 ) {
                        cout << " ";
                    }
                }
                cout << endl;
            }

            check++;

            if (twentyfourth==1 && twentyfifth == 1){
        
                ans_significand += 1;

            }
            else if ((ans_significand&1)==1 && twentyfourth ==1 && twentyfifth == 0){
   
                ans_significand += 1;

            }

            if ((ans_significand>>24)==1){
                ans_significand = (ans_significand>>1);
                ans_exp += 1;

            }
        }

        //Overflow
        else if (int(ans_exp) + (i-23) - 7 >= 255){
            ans_significand = (1<<23);
            ans_exp = 255;
        }
}


    //When answer is zero
     if (i==0 && ans_exp < 255){
        ans_exp = 0;
    }
    
    
            if (check1 == 3) {
                // Print binary representation of ans_significand in IEEE 754 format (25 bits)
                cout << "Binary rep. after rounding(23 bits): ";
                for (int j = 29; j >= 6; j--) {
                    cout << ((ans_significand >> j) & 1);
                    if (j == 31 || j == 23) {
                        cout << " ";
                    }
                }
                cout << endl;
            }

            check1++;
 

    /* Constructing floating point number from sign, exponent and significand */

    unsigned int ans = (ans_sign<<31) | (ans_exp<<23) | (ans_significand& (0x7FFFFF));
    nresult.write(ans);
    }
}


    SC_CTOR(FloatingPointNormaliser) {
        SC_THREAD(normal_process);
        sensitive << clock.pos();
    }
};

// Top-level Module
SC_MODULE(Top) {
    FloatingPointExtractor extractor;
    FloatingPointAdder adder;
    FloatingPointNormaliser normalization;
    sc_signal<bool> a_sign;
    sc_signal<sc_uint<8>> a_exp;
    sc_signal<sc_uint<32>> a_significands;
    sc_signal<bool> b_sign;
    sc_signal<sc_uint<8>> b_exp;
    sc_signal<sc_uint<32>> b_significands;
    sc_signal<bool> result_sign;
    sc_signal<sc_uint<8>> result_exp;
    sc_signal<sc_uint<32>> result_significand;
    sc_signal<sc_uint<32>> a;
    sc_signal<sc_uint<32>> b;
    sc_signal<sc_uint<32>> normalized_result;
    sc_clock clock;

    SC_CTOR(Top)
        : extractor("Extractor"),
          adder("Adder"),
          normalization("Normalization"),
          clock("clock", 1, SC_NS) {
        extractor.a(a);
        extractor.b(b);
        extractor.a_sign(a_sign);
        extractor.a_exp(a_exp);
        extractor.a_significand(a_significands);
        extractor.b_sign(b_sign);
        extractor.b_exp(b_exp);
        extractor.b_significand(b_significands);
        extractor.clock(clock);

        adder.a_sign(a_sign);
        adder.a_exp(a_exp);
        adder.a_significand(a_significands);
        adder.b_sign(b_sign);
        adder.b_exp(b_exp);
        adder.b_significand(b_significands);
        adder.result_sign(result_sign);
        adder.result_exp(result_exp);
        adder.result_significand(result_significand);
        adder.clock(clock);

        normalization.result_sign(result_sign);
        normalization.result_exp(result_exp);
        normalization.result_significand(result_significand);
        normalization.nresult(normalized_result);
        normalization.clock(clock);
    }
};

int sc_main(int argc, char* argv[]) {
    Top top("Top");
    sc_trace_file* tf = sc_create_vcd_trace_file("waveform");
    float a_float, b_float;
    cout << "Enter the value for a: ";
    cin >> a_float;
    cout << "Enter the value for b: ";
    cin >> b_float;

    unsigned int a_binary, b_binary;
    memcpy(&a_binary, &a_float, sizeof(a_binary));
    memcpy(&b_binary, &b_float, sizeof(b_binary));

    top.a_sign.write(static_cast<bool>((a_binary & 0x80000000) >> 31));
    top.b_sign.write(static_cast<bool>((b_binary & 0x80000000) >> 31));
    top.a.write(a_binary);
    top.b.write(b_binary);
    sc_trace(tf, top.a_sign, "a_sign");
    sc_trace(tf, top.a_exp, "a_exp");
    sc_trace(tf, top.a_significands, "a_significands");
    sc_trace(tf, top.b_sign, "b_sign");
    sc_trace(tf, top.b_exp, "b_exp");
    sc_trace(tf, top.b_significands, "b_significands");
    sc_trace(tf, top.result_sign, "result_sign");
    sc_trace(tf, top.result_exp, "result_exp");
    sc_trace(tf, top.result_significand, "result_significand");
    sc_trace(tf, top.normalized_result, "normalized_result");    sc_start(10, SC_NS);
    sc_close_vcd_trace_file(tf);
    unsigned int result = top.normalized_result.read();
    float result_float;
    memcpy(&result_float, &result, sizeof(result_float));
    cout << "Result: " << result_float << endl;
    return 0;
}
