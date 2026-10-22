#include <cstdint>
#include "alu.h"
// ---------------- primitive "gates" ----------------
// XOR built only from AND / OR / NOT.
static uint64_t xor_(uint64_t x, uint64_t y) { return (x & ~y) | (~x & y); } // built xor function for simplicity

static uint64_t maskOf(int width)
{
    return width >= 64 ? ~0ULL : ((1ULL << width) - 1);
}
struct AddOut
{
    uint64_t sum;
    bool cout;
};
// ---------------- Brent-Kung Parallel-Prefix Adder ----------------
// Computes carries using an up-sweep and down-sweep prefix tree.
// Each prefix node combines:
// G = G_hi | (P_hi & G_lo)
// P = P_hi & P_lo
//
// Compared with a dense prefix tree, Brent-Kung uses fewer
// prefix-combine operations, at the cost of somewhat greater depth.

static AddOut brent_kung_add(uint64_t a, uint64_t b, bool cin, int width)
{
    const uint64_t mask = maskOf(width);
    a &= mask;
    b &= mask;

    bool G[64] = {};
    bool P[64] = {};
    bool p0[64] = {};

    // Level 0: individual bit generate and propagate.
    for (int i = 0; i < width; ++i)
    {
        bool ai = (a >> i) & 1ULL;
        bool bi = (b >> i) & 1ULL;

        G[i] = ai & bi;
        P[i] = ai ^ bi;
        p0[i] = P[i];
    }

    // Up-sweep: build group generate/propagate values.
    for (int d = 1; d < width; d <<= 1)
    {
        for (int i = 2 * d - 1; i < width; i += 2 * d)
        {
            G[i] = G[i] | (P[i] & G[i - d]);
            P[i] = P[i] & P[i - d];
        }
    }

    // Down-sweep: compute the prefixes for the remaining bit positions.
    int d = 1;
    while (d < width)
        d <<= 1;
    d >>= 2;

    for (; d >= 1; d >>= 1)
    {
        for (int i = 3 * d - 1; i < width; i += 2 * d)
        {
            G[i] = G[i] | (P[i] & G[i - d]);
            P[i] = P[i] & P[i - d];
        }
    }

    // Apply the external carry-in to obtain each bit's carry-out.
    uint64_t carryOut = 0;
    for (int i = 0; i < width; ++i)
    {
        bool c = G[i] | (P[i] & cin);
        if (c)
            carryOut |= (1ULL << i);
    }

    // Carry into bit i is the carry-out from bit i-1.
    uint64_t carryIn = ((carryOut << 1) | (cin ? 1ULL : 0ULL)) & mask;

    uint64_t sum = 0;
    for (int i = 0; i < width; ++i)
    {
        bool si = p0[i] ^ ((carryIn >> i) & 1ULL);
        if (si)
            sum |= (1ULL << i);
    }

    return {sum & mask, bool((carryOut >> (width - 1)) & 1ULL)};
}

static uint32_t add32(uint32_t a, uint32_t b, bool cin = false)
{
    return (uint32_t)brent_kung_add(a, b, cin, 32).sum;
}
static uint32_t neg32(uint32_t x) { return add32(~x, 0, true); } // 2's complement

// ---------------- Booth multiplier (Sec 8.2.3) ----------------
static void booth_mul(int32_t multiplier, int32_t multiplicand, uint32_t &lo, uint32_t &hi)
{
    const int W = 33;
    const uint64_t M33 = maskOf(W);

    uint64_t U = 0;                                     // 33-bit
    uint32_t V = (uint32_t)multiplier;                  // 32-bit
    uint64_t N = (uint64_t)(int64_t)multiplicand & M33; // sign extended to 33
    uint64_t negN = brent_kung_add(~N & M33, 0, true, W).sum;  // -N
    bool prev = false;

    for (int i = 0; i < 32; ++i)
    {
        bool cur = V & 1;
        if (cur && !prev)
            U = brent_kung_add(U, negN, false, W).sum; // 10 -> U - N
        else if (!cur && prev)
            U = brent_kung_add(U, N, false, W).sum; // 01 -> U + N
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
static void nonrestoring_div(uint32_t dividend, uint32_t divisor, uint32_t &quot, uint32_t &rem)
{
    const int W = 33;
    const uint64_t M33 = maskOf(W);
    uint64_t U = 0;
    uint32_t V = dividend;
    uint64_t D = divisor & M33;
    uint64_t negD = brent_kung_add(~D & M33, 0, true, W).sum;

    for (int i = 0; i < 32; ++i)
    {
        // UV <<= 1
        U = ((U << 1) | (V >> 31)) & M33;
        V <<= 1;

        bool negative = (U >> 32) & 1;
        U = negative ? brent_kung_add(U, D, false, W).sum
                     : brent_kung_add(U, negD, false, W).sum;

        bool q = !((U >> 32) & 1);
        V |= (uint32_t)q;
    }
    if ((U >> 32) & 1)
        U = brent_kung_add(U, D, false, W).sum;
    quot = V;
    rem = (uint32_t)U;
}

// ---------------- Barrel shifter (5 mux stages) ----------------
static uint32_t shift(uint32_t x, uint32_t amt, bool left, bool arith)
{
    uint32_t fill = (arith && (x >> 31)) ? 0xFFFFFFFFu : 0;
    if (amt > 31)
        return left ? 0 : fill;
    for (int k = 0; k < 5; ++k)
    {
        if ((amt >> k) & 1)
        {
            uint32_t s = 1u << k;
            if (left)
                x = x << s;
            else
                x = (x >> s) | (fill << (32 - s));
        }
    }
    return x;
}

aluResult ALU::execute(aluop op, int32_t A, int32_t B)
{
    aluResult res{};
    const uint32_t a = (uint32_t)A, b = (uint32_t)B;

    switch (op)
    {

    case aluop::ADD:
        res.val = (int32_t)add32(a, b);
        break;

    case aluop::SUB: // A + ~B + 1
        res.val = (int32_t)add32(a, ~b, true);
        break;

    case aluop::MUL:
    {
        uint32_t lo, hi;
        booth_mul(B, A, lo, hi); // multiplier B, multiplicand A
        res.val = (int32_t)lo;
        res.hi = (int32_t)hi;
        break;
    }

    case aluop::DIV:
    case aluop::MOD:
    {
        if (B == 0)
        {
            res.flag_ERR = true;
            break;
        }
        bool sa = a >> 31, sb = b >> 31;
        uint32_t ua = sa ? neg32(a) : a; // |A|
        uint32_t ub = sb ? neg32(b) : b; // |B|  (<= 2^31)
        uint32_t q, r;
        nonrestoring_div(ua, ub, q, r);
        if (sa != sb)
            q = neg32(q); // quotient sign
        if (sa)
            r = neg32(r); // remainder takes dividend's sign
        res.val = (int32_t)(op == aluop::DIV ? q : r);
        break;
    }

    case aluop::CMP:
    {
        uint32_t d = add32(a, ~b, true); // A - B
        bool signA = a >> 31, signB = b >> 31, signD = d >> 31;
        bool V = (signA && !signB && !signD) || (!signA && signB && signD);
        bool LT = (signD || V) && !(signD && V); // N xor V
        res.val = (int32_t)d;
        res.flag_E = (d == 0);
        res.flag_GT = !res.flag_E && !LT;
        break;
    }
    case aluop::LAND:
    {
        if (A != 0)
        {
            if (B != 0)
            {
                res.flag_LAND = true;
            }
            else
            {
                res.flag_LAND = false;
            }
        }
        else
        {
            res.flag_LAND = false;
        }
        break;
    }
    case aluop::LOR:
    {
        if (A != 0)
        {

            res.flag_LOR = true;
        }
        else if (B != 0)
        {
            res.flag_LOR = true;
        }
        else
        {
            res.flag_LOR = false;
        }
        break;
    }

    case aluop::LNOT:
    {
        if (A != 0)
        {
            res.flag_LNOT = false;
        }
        else
        {
            res.flag_LNOT = true;
        }
        break;
    }
    case aluop::AND:
        res.val = A & B;
        break;
    case aluop::OR:
        res.val = A | B;
        break;
    case aluop::NOT:
        res.val = ~A;
        break;
    case aluop::MOV:
        res.val = B;
        break; // MOV Rd, Op2 style

    case aluop::LSL:
        res.val = (int32_t)shift(a, b, true, false);
        break;
    case aluop::LSR:
        res.val = (int32_t)shift(a, b, false, false);
        break;
    case aluop::ASR:
        res.val = (int32_t)shift(a, b, false, true);
        break;
    }
    return res;
};