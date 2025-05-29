#include <systemc.h>

//==============================================================================
//
// Module: ieee754_extractor
//
SC_MODULE(ieee754_extractor)
{
    sc_in<sc_uint<32> > A;
    sc_out<bool> sign;
    sc_out<sc_uint<8> > exponent;
    sc_out<sc_uint<24> > mantissa;

    void process() {
        sign.write(A.read()[31]);
        exponent.write(A.read().range(30, 23));
        if (exponent.read() == 0) {
            mantissa.write((sc_uint<24>)((sc_uint<1>(0), A.read().range(22, 0))));
        } else {
            mantissa.write((sc_uint<24>)((sc_uint<1>(1), A.read().range(22, 0))));
        }
    }

    SC_CTOR(ieee754_extractor) {
        SC_METHOD(process);
        sensitive << A<<sign<<exponent<<mantissa;
    }
};

//==============================================================================
//
// Module: ieee754_adder_core
//
SC_MODULE(ieee754_adder_core)
{
    sc_in<sc_uint<8> > exp_a, exp_b;
    sc_in<sc_uint<24> > mant_a, mant_b;
    sc_in<bool> sign_a, sign_b;
    sc_out<bool> out_sign;
    sc_out<sc_uint<8> > out_exponent;
    sc_out<sc_uint<25> > out_mantissa;

    void process() {
        sc_uint<8> diff = 0;
        sc_uint<24> tmp_mantissa = 0;
        bool a_is_nan = (exp_a.read() == 0xFF) && (mant_a.read().range(22, 0) != 0);
        bool b_is_nan = (exp_b.read() == 0xFF) && (mant_b.read().range(22, 0) != 0);
        bool a_is_inf = (exp_a.read() == 0xFF) && (mant_a.read().range(22, 0) == 0);
        bool b_is_inf = (exp_b.read() == 0xFF) && (mant_b.read().range(22, 0) == 0);

        if (a_is_nan || b_is_nan) {
            out_exponent.write(0xFF);
            out_mantissa.write(0x400000);
            out_sign.write(false);
        } else {
            if (a_is_inf || b_is_inf) {
                if (a_is_inf && b_is_inf) {
                    if (sign_a.read() == sign_b.read()) {
                        out_exponent.write(0xFF);
                        out_mantissa.write(0);
                        out_sign.write(sign_a.read());
                    } else {
                        out_exponent.write(0xFF);
                        out_mantissa.write(0x400000);
                        out_sign.write(false);
                    }
                } else {
                    out_exponent.write(0xFF);
                    out_mantissa.write(0);
                    out_sign.write(a_is_inf ? sign_a.read() : sign_b.read());
                }
            } else {
                if (exp_a.read() == 0 && mant_a.read() == 0) {
                    out_sign.write(sign_b.read());
                    out_exponent.write(exp_b.read());
                    out_mantissa.write((sc_uint<25>)((sc_uint<1>(0), mant_b.read())));
                } else if (exp_b.read() == 0 && mant_b.read() == 0) {
                    out_sign.write(sign_a.read());
                    out_exponent.write(exp_a.read());
                    out_mantissa.write((sc_uint<25>)((sc_uint<1>(0), mant_a.read())));
                } else {
                    if (exp_a.read() == 0) {
                        out_exponent.write(exp_b.read());
                        tmp_mantissa = mant_a.read();
                    } else if (exp_b.read() == 0) {
                        out_exponent.write(exp_a.read());
                        tmp_mantissa = mant_b.read();
                    } else {
                        out_exponent.write((exp_a.read() > exp_b.read()) ? exp_a.read() : exp_b.read());
                    }

                    if (exp_a.read() > exp_b.read()) {
                        diff = exp_a.read() - exp_b.read();
                        tmp_mantissa = mant_b.read() >> diff;
                        if (sign_a.read() == sign_b.read()) {
                            out_mantissa.write((sc_uint<25>)((sc_uint<1>(0), mant_a.read())) + 
                                            (sc_uint<25>)((sc_uint<1>(0), tmp_mantissa)));
                        } else {
                            if (mant_a.read() >= tmp_mantissa) {
                                out_mantissa.write((sc_uint<25>)((sc_uint<1>(0), mant_a.read())) - 
                                              (sc_uint<25>)((sc_uint<1>(0), tmp_mantissa)));
                            } else {
                                out_mantissa.write((sc_uint<25>)((sc_uint<1>(0), tmp_mantissa)) - 
                                              (sc_uint<25>)((sc_uint<1>(0), mant_a.read())));
                            }
                        }
                        out_sign.write((mant_a.read() >= tmp_mantissa) ? sign_a.read() : sign_b.read());
                    } else if (exp_b.read() > exp_a.read()) {
                        diff = exp_b.read() - exp_a.read();
                        tmp_mantissa = mant_a.read() >> diff;
                        if (sign_a.read() == sign_b.read()) {
                            out_mantissa.write((sc_uint<25>)((sc_uint<1>(0), mant_b.read())) + 
                                            (sc_uint<25>)((sc_uint<1>(0), tmp_mantissa)));
                        } else {
                            if (mant_b.read() >= tmp_mantissa) {
                                out_mantissa.write((sc_uint<25>)((sc_uint<1>(0), mant_b.read())) - 
                                              (sc_uint<25>)((sc_uint<1>(0), tmp_mantissa)));
                            } else {
                                out_mantissa.write((sc_uint<25>)((sc_uint<1>(0), tmp_mantissa)) - 
                                              (sc_uint<25>)((sc_uint<1>(0), mant_b.read())));
                            }
                        }
                        out_sign.write((mant_b.read() >= tmp_mantissa) ? sign_b.read() : sign_a.read());
                    } else {
                        if (sign_a.read() == sign_b.read()) {
                            out_mantissa.write((sc_uint<25>)((sc_uint<1>(0), mant_a.read())) + 
                                            (sc_uint<25>)((sc_uint<1>(0), mant_b.read())));
                        } else {
                            if (mant_a.read() > mant_b.read()) {
                                out_mantissa.write((sc_uint<25>)((sc_uint<1>(0), mant_a.read())) - 
                                              (sc_uint<25>)((sc_uint<1>(0), mant_b.read())));
                            } else {
                                out_mantissa.write((sc_uint<25>)((sc_uint<1>(0), mant_b.read())) - 
                                              (sc_uint<25>)((sc_uint<1>(0), mant_a.read())));
                            }
                        }
                        out_sign.write((mant_a.read() > mant_b.read()) ? sign_a.read() : sign_b.read());
                    }

                    if (out_mantissa.read() == 0) {
                        out_sign.write(false);
                        out_exponent.write(0);
                    }
                }
            }
        }
    }

    SC_CTOR(ieee754_adder_core) {
        SC_METHOD(process);
        sensitive << exp_a << exp_b << mant_a << mant_b << sign_a << sign_b<<out_mantissa;
    }
};

//==============================================================================
//
// Module: ieee754_normalizer
//
SC_MODULE(ieee754_normalizer)
{
    sc_in<sc_uint<8> > exponent;
    sc_in<sc_uint<25> > mantissa;
    sc_in<bool> sign;
    sc_out<sc_uint<32> > result;

    void process() {
        sc_uint<5> lz = 0;
        sc_uint<8> norm_exponent = 0;
        sc_uint<25> norm_mantissa = 0;

        if (exponent.read() == 0xFF) {
            result.write((sc_uint<32>)((sign.read(), exponent.read(), mantissa.read().range(22, 0))));
        } else {
            if (mantissa.read() == 0) {
                result.write(0);
            } else {
                norm_exponent = exponent.read();
                norm_mantissa = mantissa.read();

                if (norm_mantissa[24]) {
                    norm_exponent = norm_exponent + 1;
                    norm_mantissa = norm_mantissa >> 1;
                } else if (norm_mantissa[23] == 0 && norm_exponent != 0) {
                    for (lz = 0; lz < 24 && norm_mantissa[23 - lz] == 0; lz++) {
                        // The loop counter is automatically incremented
                    }
                    if (norm_exponent > lz) {
                        norm_exponent = norm_exponent - lz;
                        norm_mantissa = norm_mantissa << lz;
                    } else {
                        norm_mantissa = norm_mantissa << (norm_exponent - 1);
                        norm_exponent = 0;
                    }
                }

                if (norm_exponent >= 0xFF) {
                    result.write(0);
                } else {
                    result.write((sc_uint<32>)((sign.read(), norm_exponent, norm_mantissa.range(22, 0))));
                }
            }
        }
    }

    SC_CTOR(ieee754_normalizer) {
        SC_METHOD(process);
        sensitive << exponent << mantissa << sign;
    }
};

//==============================================================================
//
// Module: ieee754_adder
//
SC_MODULE(ieee754_adder)
{
    sc_in<sc_uint<32> > A, B;
    sc_out<sc_uint<32> > O;

    // Internal signals
    sc_signal<bool> sign_a, sign_b, out_sign;
    sc_signal<sc_uint<8> > exp_a, exp_b, out_exponent;
    sc_signal<sc_uint<24> > mant_a, mant_b;
    sc_signal<sc_uint<25> > out_mantissa;

    // Submodules
    ieee754_extractor *extractA;
    ieee754_extractor *extractB;
    ieee754_adder_core *adderCore;
    ieee754_normalizer *normalizer;

    SC_CTOR(ieee754_adder) {
        // Create submodules
        extractA = new ieee754_extractor("extractA");
        extractA->A(A);
        extractA->sign(sign_a);
        extractA->exponent(exp_a);
        extractA->mantissa(mant_a);

        extractB = new ieee754_extractor("extractB");
        extractB->A(B);
        extractB->sign(sign_b);
        extractB->exponent(exp_b);
        extractB->mantissa(mant_b);

        adderCore = new ieee754_adder_core("adderCore");
        adderCore->exp_a(exp_a);
        adderCore->exp_b(exp_b);
        adderCore->mant_a(mant_a);
        adderCore->mant_b(mant_b);
        adderCore->sign_a(sign_a);
        adderCore->sign_b(sign_b);
        adderCore->out_sign(out_sign);
        adderCore->out_exponent(out_exponent);
        adderCore->out_mantissa(out_mantissa);

        normalizer = new ieee754_normalizer("normalizer");
        normalizer->exponent(out_exponent);
        normalizer->mantissa(out_mantissa);
        normalizer->sign(out_sign);
        normalizer->result(O);
    }

};


