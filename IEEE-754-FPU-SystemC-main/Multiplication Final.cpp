#include <systemc.h>
#include <iostream>
#include <bitset>
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

            // Special cases
            if (a_exp0 == 255 && a_significand0 != 0) {
                // Case when a is NaN
                a_sign.write(true);
                a_exp.write(255);
                a_significand.write(0x7fffffff);
                b_sign.write(true);
                b_exp.write(255);
                b_significand.write(0x7fffffff);
            } else if (b_exp0 == 255 && b_significand0 != 0) {
                // Case when b is NaN
                a_sign.write(true);
                a_exp.write(255);
                a_significand.write(0x7fffffff);
                b_sign.write(true);
                b_exp.write(255);
                b_significand.write(0x7fffffff);
            } else if (a_exp0 == 255 && a_significand0 == 0 && b_exp0 == 255 && b_significand0 == 0 && a_sign0 != b_sign0) {
                // Case when Infinity - Infinity
                a_sign.write(true);
                a_exp.write(255);
                a_significand.write(0x7fffffff);
                b_sign.write(true);
                b_exp.write(255);
                b_significand.write(0x7fffffff);
            } else if (a_exp0 == 255 && a_significand0 == 0) {
                // Case when a is Infinity
                a_sign.write(a_sign0);
                a_exp.write(255);
                a_significand.write(0x7fffffff);
                b_sign.write(true);
                b_exp.write(255);
                b_significand.write(0x7fffffff);
            } else if (b_exp0 == 255 && b_significand0 == 0) {
                // Case when b is Infinity
                a_sign.write(true);
                a_exp.write(255);
                a_significand.write(0x7fffffff);
                b_sign.write(b_sign0);
                b_exp.write(255);
                b_significand.write(0x7fffffff);
            } else {
                // Normal case
                a_sign.write(a_sign0);
                a_exp.write(a_exp0);
                a_significand.write(a_significand0);
                b_sign.write(b_sign0);
                b_exp.write(b_exp0);
                b_significand.write(b_significand0);
            }
        }
    }

    SC_CTOR(FloatingPointExtractor) : a("a"), b("b"), a_sign("a_sign"), a_exp("a_exp"), a_significand("a_significand"),
                                     b_sign("b_sign"), b_exp("b_exp"), b_significand("b_significand"), clock("clock") {
        SC_THREAD(extraction_process);
        sensitive << clock.pos();
    }
};

// FloatingPointMultiplier Module
SC_MODULE(FloatingPointMultiplier) {
    sc_in<bool> a_sign;
    sc_in<sc_uint<8>> a_exp;
    sc_in<sc_uint<32>> a_significand;
    sc_in<bool> b_sign;
    sc_in<sc_uint<8>> b_exp;
    sc_in<sc_uint<32>> b_significand;
    sc_out<bool> result_sign;
    sc_out<sc_uint<8>> result_exp;
    sc_out<sc_uint<32>> result_significand;
    sc_out<sc_uint<32>> result_significand1;
    sc_in<bool> clock;

    void multiply_process() {
        while (true) {
            wait();
            bool aSign = a_sign.read();
            unsigned int aExponent = a_exp.read();
            unsigned int aSignificand = a_significand.read();

            bool bSign = b_sign.read();
            unsigned int bExponent = b_exp.read();
            unsigned int bSignificand = b_significand.read();

            // compute sign bit
            bool resultSign = aSign ^ bSign;

            // compute exponent
            unsigned int resultExponent = aExponent + bExponent - 0x7F;

            // add implicit `1' bit
            aSignificand = (aSignificand | 0x00800000) << 7;
            bSignificand = (bSignificand | 0x00800000) << 8;

            uint64_t resultSignificand = static_cast<uint64_t>(aSignificand) * static_cast<uint64_t>(bSignificand);

            uint32_t resultSignificand0 = static_cast<uint32_t>(resultSignificand >> 32);
            uint32_t resultSignificand1 = static_cast<uint32_t>(resultSignificand & 0xFFFFFFFF);

            result_sign.write(resultSign);
            result_exp.write(resultExponent);
            result_significand.write(resultSignificand0);
            result_significand1.write(resultSignificand1);
        }
    }

    SC_CTOR(FloatingPointMultiplier) : a_sign("a_sign"), a_exp("a_exp"), a_significand("a_significand"),
                                       b_sign("b_sign"), b_exp("b_exp"), b_significand("b_significand"),
                                       result_sign("result_sign"), result_exp("result_exp"),
                                       result_significand("result_significand"), clock("clock") {
        SC_THREAD(multiply_process);
        sensitive << clock.pos();
    }
};

// FloatingPointNormalizer Module
SC_MODULE(FloatingPointNormalizer) {
    sc_in<bool> result_sign;
    sc_in<sc_uint<8>> result_exp;
    sc_in<sc_uint<32>> result_significand;
    sc_in<sc_uint<32>> result_significand1;
    sc_out<sc_uint<32>> normalized_result;
    sc_in<bool> clock;
    
void normalize_process() { int check=0,check1=0;
    while (true) {
        wait();

        bool resultSign = result_sign.read();
        unsigned int resultExponent = result_exp.read();
        unsigned int resultSignificand0 = result_significand.read();
        unsigned int resultSignificand1 = result_significand1.read();
        // check if we overflowed into more than 23-bits and handle accordingly
        resultSignificand0 |= (resultSignificand1 != 0);
        if (0 <= static_cast<int32_t>(resultSignificand0 << 1)) {
            resultSignificand0 <<= 1;
            resultExponent--;
        }

        
        
        int bit_value;
           int arr[23];
        int j=22,k=0;
    for (int i = 29; i >= 7; --i) {
   int bit_value = (resultSignificand0 >> i) & 1;
 k++;
   arr[j]=bit_value;
   j--;
       }
       
unsigned int temp1;
        for (int i = 22; i >=0; --i) {
        temp1 = (temp1 << 1) | arr[i];
    }
    


bool bit24,bit25;

  
  
    
if(bit24 ==1 & bit25==1 || bit24==1 && bit25==0)
  temp1+=1;



int arr1[23];
for(int k=22;k>=0;--k)
{
   arr1[k]=(temp1>>k)&1;
  }


int f=22;
for (int l = 29; l >= 7; l--)
{
   // Check if f is within bounds
  if (f >= 0)
  {
      // Shift and copy arr1[f] to the l-th position
       resultSignificand0 |= (arr1[f]) << l;

      // Update f
      f--;
  }
}
        
 uint32_t result_value = (resultSign << 31) | ((resultExponent << 23) + (resultSignificand0>>7));
check++;
        normalized_result.write(result_value);
    }
}

    
    SC_CTOR(FloatingPointNormalizer) : result_sign("result_sign"), result_exp("result_exp"),
    result_significand("result_significand"), normalized_result("normalized_result"),
    clock("clock") {
        SC_THREAD(normalize_process);
        sensitive << clock.pos();
    }
};

SC_MODULE(Top) {
    FloatingPointExtractor extractor;
    FloatingPointMultiplier multiplier;
    FloatingPointNormalizer normalizer;
    sc_signal<bool> a_sign;
    sc_signal<sc_uint<8>> a_exp;
    sc_signal<sc_uint<32>> a_significand;
    sc_signal<bool> b_sign;
    sc_signal<sc_uint<8>> b_exp;
    sc_signal<sc_uint<32>> b_significand;
    sc_signal<bool> result_sign;
    sc_signal<sc_uint<8>> result_exp;
    sc_signal<sc_uint<32>> result_significand;
    sc_signal<sc_uint<32>> result_significand0;
    sc_signal<sc_uint<32>> a;
    sc_signal<sc_uint<32>> b;
    sc_signal<sc_uint<32>> normalized_result;
    sc_clock clock;

    SC_CTOR(Top) : extractor("Extractor"), multiplier("Multiplier"), normalizer("Normalizer") {
        extractor.a(a);
        extractor.b(b);
        extractor.a_sign(a_sign);
        extractor.a_exp(a_exp);
        extractor.a_significand(a_significand);
        extractor.b_sign(b_sign);
        extractor.b_exp(b_exp);
        extractor.b_significand(b_significand);
        extractor.clock(clock);

        multiplier.a_sign(a_sign);
        multiplier.a_exp(a_exp);
        multiplier.a_significand(a_significand);
        multiplier.b_sign(b_sign);
        multiplier.b_exp(b_exp);
        multiplier.b_significand(b_significand);
        multiplier.result_sign(result_sign);
        multiplier.result_exp(result_exp);
        multiplier.result_significand(result_significand);
        multiplier.result_significand1(result_significand0);
        multiplier.clock(clock);

        normalizer.result_sign(result_sign);
        normalizer.result_exp(result_exp);
        normalizer.result_significand(result_significand);
        normalizer.result_significand1(result_significand0);
        normalizer.normalized_result(normalized_result);
        normalizer.clock(clock);
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
    sc_trace(tf, top.a_significand, "a_significand");
    sc_trace(tf, top.b_sign, "b_sign");
    sc_trace(tf, top.b_exp, "b_exp");
    sc_trace(tf, top.b_significand, "b_significand");
    sc_trace(tf, top.result_sign, "result_sign");
    sc_trace(tf, top.result_exp, "result_exp");
    sc_trace(tf, top.result_significand, "result_significand");
    sc_trace(tf, top.normalized_result, "normalized_result");    sc_start(10, SC_NS);
    sc_close_vcd_trace_file(tf);    unsigned int result = top.normalized_result.read();
    float result_float;
    memcpy(&result_float, &result, sizeof(result_float));

    cout << "Result: " << result_float << endl;

    return 0;
}

