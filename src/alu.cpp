#pragma once
#include <cstdint>

enum class aluop {
    ADD, SUB, MUL, DIV, MOD, CMP, AND, OR, NOT, MOV, LSL, LSR, ASR
};

struct aluResult {
    int32_t val = 0;        // main 32-bit result (MUL: low word, MOD: remainder)
    int32_t hi = 0;         // MUL: high 32 bits of the 64-bit product
    bool flag_E = false;    // CMP: A == B
    bool flag_GT = false;   // CMP: A > B (signed)
    bool flag_ERR = false;  // DIV/MOD by zero
};

class ALU {
    // ---------------- primitive "gates" ----------------
    // XOR built only from AND / OR / NOT, as in the chapter's half adder.
    static uint64_t xor_(uint64_t x, uint64_t y) { return (x & ~y) | (~x & y); }//built xor function for simplicity

    static uint64_t maskOf(int width) {
        return width >= 64 ? ~0ULL : ((1ULL << width) - 1);
    }

    // ---------------- Carry Lookahead Adder (Sec 8.1.5(of sarangi book)) ----------------
    struct AddOut { uint64_t sum; bool cout; };

    // Word-parallel version of the (G,P) tree. Every bit position is processed
    // at once; each loop iteration is one tree level -> log2(width) levels.
    // Combine rule (Eq. 8.9):  G = G_hi | (P_hi & G_lo),  P = P_hi & P_lo
    static AddOut cla_add(uint64_t a, uint64_t b, bool cin, int width) {
        const uint64_t mask = maskOf(width);
        a &= mask; b &= mask;

        // Stage I, level 0: per-bit generate / propagate
        // base cases for stage #1
        const uint64_t g0 = a & b;
        const uint64_t p0 = xor_(a, b);
        uint64_t G = g0, P = p0;

        // After the loop, bit i of (G,P) describes the range [i..0]
        for (int d = 1; d < width; d <<= 1) {
            uint64_t lowKeep = (1ULL << d) - 1;           // bits with no partner keep P
            uint64_t newG = G | (P & (G << d));
            uint64_t newP = P & ((P << d) | lowKeep);
            G = newG & mask; P = newP & mask;
        }

        // Stage II: carry into bit i+1 is  G[i..0] | P[i..0] & cin
        const uint64_t cinMask = cin ? mask : 0;
        const uint64_t carryOut = (G | (P & cinMask)) & mask;   // bit i = carry OUT of bit i
        const uint64_t carryIn  = ((carryOut << 1) | (cin ? 1 : 0)) & mask; // bit i = carry INTO bit i

        AddOut r;
        r.sum  = xor_(p0, carryIn) & mask;
        r.cout = (carryOut >> (width - 1)) & 1;
        return r;
    }

    static uint32_t add32(uint32_t a, uint32_t b, bool cin = false) {
        return (uint32_t)cla_add(a, b, cin, 32).sum;
    }
    static uint32_t neg32(uint32_t x) { return add32(~x, 0, true); }  // 2's complement

    // ---------------- Booth multiplier (Sec 8.2.3) ----------------
    static void booth_mul(int32_t multiplier, int32_t multiplicand,
                          uint32_t &lo, uint32_t &hi) {
        const int W = 33;
        const uint64_t M33 = maskOf(W);

        uint64_t U = 0;                       // 33-bit
        uint32_t V = (uint32_t)multiplier;    // 32-bit
        uint64_t N    = (uint64_t)(int64_t)multiplicand & M33;  // sign extended to 33
        uint64_t negN = cla_add(~N & M33, 0, true, W).sum;       // -N
        bool prev = false;

        for (int i = 0; i < 32; ++i) {
            bool cur = V & 1;
            if (cur && !prev)       U = cla_add(U, negN, false, W).sum;  // 10 -> U - N
            else if (!cur && prev)  U = cla_add(U, N,    false, W).sum;  // 01 -> U + N
            prev = cur;

            // arithmetic right shift of the 65-bit register UV
            uint64_t sign = (U >> 32) & 1;
            V = (V >> 1) | ((uint32_t)(U & 1) << 31);
            U = (U >> 1) | (sign << 32);
        }
        lo = V;
        hi = (uint32_t)(U & 0xFFFFFFFFULL);
    }

    // ---------------- Non-restoring divider (Sec 8.3.3) ----------------
    // Unsigned divide; D must be <= 2^31 and non-zero so 33-bit U never overflows.
    static void nonrestoring_div(uint32_t dividend, uint32_t divisor,
                                 uint32_t &quot, uint32_t &rem) {
        const int W = 33;
        const uint64_t M33 = maskOf(W);
        uint64_t U = 0;
        uint32_t V = dividend;
        uint64_t D    = divisor & M33;
        uint64_t negD = cla_add(~D & M33, 0, true, W).sum;

        for (int i = 0; i < 32; ++i) {
            // UV <<= 1
            U = ((U << 1) | (V >> 31)) & M33;
            V <<= 1;

            bool negative = (U >> 32) & 1;
            U = negative ? cla_add(U, D, false, W).sum
                         : cla_add(U, negD, false, W).sum;

            bool q = !((U >> 32) & 1);
            V |= (uint32_t)q;
        }
        if ((U >> 32) & 1) U = cla_add(U, D, false, W).sum;   // final correction
        quot = V;
        rem  = (uint32_t)U;
    }

    // ---------------- Barrel shifter (5 mux stages) ----------------
    static uint32_t shift(uint32_t x, uint32_t amt, bool left, bool arith) {
        uint32_t fill = (arith && (x >> 31)) ? 0xFFFFFFFFu : 0;
        if (amt > 31) return left ? 0 : fill;
        for (int k = 0; k < 5; ++k) {
            if ((amt >> k) & 1) {
                uint32_t s = 1u << k;
                if (left) x = x << s;
                else      x = (x >> s) | (fill << (32 - s));
            }
        }
        return x;
    }

public:
    aluResult execute(aluop op, int32_t A, int32_t B) {
        aluResult res{};
        const uint32_t a = (uint32_t)A, b = (uint32_t)B;

        switch (op) {

            case aluop::ADD:
                res.val = (int32_t)add32(a, b);
                break;

            case aluop::SUB:                      // A + ~B + 1
                res.val = (int32_t)add32(a, ~b, true);
                break;

            case aluop::MUL: {
                uint32_t lo, hi;
                booth_mul(B, A, lo, hi);          // multiplier B, multiplicand A
                res.val = (int32_t)lo;
                res.hi  = (int32_t)hi;
                break;
            }

            case aluop::DIV:
            case aluop::MOD: {
                if (B == 0) { res.flag_ERR = true; break; }
                bool sa = a >> 31, sb = b >> 31;
                uint32_t ua = sa ? neg32(a) : a;  // |A|
                uint32_t ub = sb ? neg32(b) : b;  // |B|  (<= 2^31)
                uint32_t q, r;
                nonrestoring_div(ua, ub, q, r);
                if (sa != sb) q = neg32(q);       // quotient sign
                if (sa)       r = neg32(r);       // remainder takes dividend's sign
                res.val = (int32_t)(op == aluop::DIV ? q : r);
                break;
            }

            case aluop::CMP: {
                uint32_t d = add32(a, ~b, true);  // A - B
                bool signA = a >> 31, signB = b >> 31, signD = d >> 31;
                bool V = (signA && !signB && !signD) || (!signA && signB && signD);
                bool LT = (signD || V) && !(signD && V);   // N xor V
                res.val    = (int32_t)d;
                res.flag_E = (d == 0);
                res.flag_GT = !res.flag_E && !LT;
                break;
            }

            case aluop::AND: res.val = A & B;  break;
            case aluop::OR:  res.val = A | B;  break;
            case aluop::NOT: res.val = ~A;     break;
            case aluop::MOV: res.val = B;      break;   // MOV Rd, Op2 style

            case aluop::LSL: res.val = (int32_t)shift(a, b, true,  false); break;
            case aluop::LSR: res.val = (int32_t)shift(a, b, false, false); break;
            case aluop::ASR: res.val = (int32_t)shift(a, b, false, true);  break;
        }
        return res;
    }
};