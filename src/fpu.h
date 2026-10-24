#pragma once
// ============================================================================
//  fpu.h  -  IEEE 754 single-precision floating point unit
//
//  Format (from the ALU Design / Computer Arithmetic slides):
//      | S (1) | E (8, biased by 127) | M (23, mantissa, hidden leading 1) |
//      normal : (-1)^S * (1 + M/2^23) * 2^(E-127),   1 <= E <= 254
//      E = 0   , M = 0  -> +/- 0
//      E = 0   , M != 0 -> denormal  (0.M) * 2^-126
//      E = 255 , M = 0  -> +/- infinity
//      E = 255 , M != 0 -> NaN
//
//  The unit is used exactly like the integer ALU:
//
//      FPU fpu;
//      fpuResult r = fpu.execute(fpuop::FADD, A, B);   // A, B = raw 32-bit patterns
//      regs[rd] = r.val;                               // r.val = raw 32-bit pattern
//
//  Registers keep the raw bit pattern, so nothing else in the CPU has to
//  know that a register holds a float.
// ============================================================================
#include <cstdint>
#include <string>
#include "alu.h"

enum class fpuop
{
    FADD,  // A + B
    FSUB,  // A - B
    FMUL,  // A * B
    FDIV,  // A / B
    FSQRT, // sqrt(A)          (B ignored)
    ITOF,  // (float) A        A is a signed 32-bit integer
    FTOI,  // (int) A          A is a float, truncates toward zero (like a C cast)
    FCMP   // compare A with B: sets flag_E (A == B) and flag_GT (A > B); val is not used
};

// The four IEEE 754 rounding modes (slides: "IEEE 754 Rounding Modes").
enum class RoundingMode
{
    NEAREST_EVEN,   // round to nearest, ties to even   (default)
    TOWARD_ZERO,    // truncation
    TOWARD_POS_INF, // round to +infinity
    TOWARD_NEG_INF  // round to -infinity
};

struct fpuResult
{
    int32_t val = 0; // result bit pattern (float for most ops, integer for FTOI)

    // IEEE 754 exception flags (never trap; the unit always returns a value)
    bool flag_INVALID = false;   // 0/0, inf-inf, 0*inf, sqrt(-1), signalling NaN, FTOI out of range
    bool flag_DIVZERO = false;   // x / 0, x != 0
    bool flag_OVERFLOW = false;  // result too large -> inf (or max finite)
    bool flag_UNDERFLOW = false; // tiny and inexact result
    bool flag_INEXACT = false;   // result had to be rounded

    // FCMP results (same meaning as the ALU CMP flags, so beq / bgt work unchanged)
    bool flag_E = false;         // A == B   (+0 == -0)
    bool flag_GT = false;        // A >  B
    bool flag_UNORDERED = false; // A or B is NaN: E and GT are both false
};

class FPU
{
public:
    RoundingMode mode = RoundingMode::NEAREST_EVEN; // used by FADD/FSUB/FMUL/FDIV/FSQRT/ITOF

    fpuResult execute(fpuop op, int32_t A, int32_t B = 0);

    // Individual operations (same thing execute() calls)
    fpuResult add(uint32_t a, uint32_t b);
    fpuResult sub(uint32_t a, uint32_t b);
    fpuResult mul(uint32_t a, uint32_t b);
    fpuResult div(uint32_t a, uint32_t b);
    fpuResult sqrt(uint32_t a);
    fpuResult intToFloat(int32_t a);
    fpuResult floatToInt(uint32_t a);
    fpuResult compare(uint32_t a, uint32_t b);

    // Helpers for tests / printing
    static float bitsToFloat(int32_t bits);
    static int32_t floatToBits(float f);
    static std::string describe(int32_t bits); // "S=0 E=127 M=0x000000  (1)"
};