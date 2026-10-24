#include "fpu.h"
#include <cstdio>
#include <cstring>

// ============================================================================
//  Internal building blocks
// ============================================================================
/*
Constant	    Purpose
BIAS = 127	    Converts between stored and unbiased exponents
E_MIN = -126    Smallest exponent for a normal number
E_MAX = 127	    Largest exponent for a normal number
SIGN_MASK	    Selects the sign bit
QNAN_BIT	    Marks a NaN as quiet
DEFAULT_NAN	    Standard NaN bit pattern returned for certain invalid operations
POS_INF	        Positive infinity
MAX_FINITE	    Largest finite positive single-precision value
*/
static constexpr int32_t BIAS = 127;
static constexpr int32_t E_MIN = -126; 
static constexpr int32_t E_MAX = 127;
static constexpr uint32_t SIGN_MASK = 0x80000000u;
static constexpr uint32_t QNAN_BIT = 0x00400000u;
static constexpr uint32_t DEFAULT_NAN = 0x7FC00000u;
static constexpr uint32_t POS_INF = 0x7F800000u;
static constexpr uint32_t MAX_FINITE = 0x7F7FFFFFu;
static ALU fpuALU;

static uint32_t aluAdd32(uint32_t a, uint32_t b)
{
    return static_cast<uint32_t>(fpuALU.execute(
        aluop::ADD, static_cast<int32_t>(a), static_cast<int32_t>(b)).val);
}

static uint32_t aluSub32(uint32_t a, uint32_t b)
{
    return static_cast<uint32_t>(fpuALU.execute(
        aluop::SUB, static_cast<int32_t>(a), static_cast<int32_t>(b)).val);
}

static uint32_t neg32(uint32_t x) { return aluSub32(0, x); }

static uint64_t add64(uint64_t a, uint64_t b)
{
    const uint32_t alo = static_cast<uint32_t>(a), blo = static_cast<uint32_t>(b);
    const uint32_t lo = aluAdd32(alo, blo);
    const uint32_t carry = lo < alo;
    const uint32_t hi0 = aluAdd32(static_cast<uint32_t>(a >> 32),
                                  static_cast<uint32_t>(b >> 32));
    const uint32_t hi = aluAdd32(hi0, carry);
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

static uint64_t sub64(uint64_t a, uint64_t b)
{
    const uint32_t alo = static_cast<uint32_t>(a), blo = static_cast<uint32_t>(b);
    const uint32_t lo = aluSub32(alo, blo);
    const uint32_t borrow = alo < blo;
    const uint32_t hi0 = aluSub32(static_cast<uint32_t>(a >> 32),
                                  static_cast<uint32_t>(b >> 32));
    const uint32_t hi = aluSub32(hi0, borrow);
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

static bool geq64(uint64_t a, uint64_t b)
{
    const uint32_t ahi = static_cast<uint32_t>(a >> 32);
    const uint32_t bhi = static_cast<uint32_t>(b >> 32);
    if (ahi != bhi) return ahi > bhi;
    return static_cast<uint32_t>(a) >= static_cast<uint32_t>(b);
}

static int32_t iadd(int32_t a, int32_t b)
{
    return static_cast<int32_t>(aluAdd32(static_cast<uint32_t>(a),
                                         static_cast<uint32_t>(b)));
}

static int32_t isub(int32_t a, int32_t b)
{
    return static_cast<int32_t>(aluSub32(static_cast<uint32_t>(a),
                                         static_cast<uint32_t>(b)));
}

// ---------------- small helpers ----------------
static int leadingBit(uint64_t m) // position of the highest 1 (priority encoder), -1 if m == 0
{
    for (int p = 63; p >= 0; --p)
        if ((m >> p) & 1)
            return p;
    return -1;
}

// Shift right and OR every bit that falls off into bit 0 ("sticky").
static uint64_t shiftRightSticky(uint64_t m, int d)
{
    if (d <= 0)
        return m;
    if (d >= 64)
        return m != 0 ? 1 : 0;
    const uint64_t lost = m & ((1ULL << d) - 1);
    return (m >> d) | (lost != 0 ? 1ULL : 0ULL);
}

// ---------------- unpacking (slide: "Unpack the E fields / significand") ----------------
// in unpacking we are computing all the fields of the 32 bit number/float that we have received
/*
It stores:
sign: whether the number is negative.
exp: the unbiased exponent.
sig: the 24-bit significand, including the implicit leading 1 for normal numbers.
cls: whether the input is zero, normal, infinity or NaN.
signaling: whether a NaN is signaling rather than quiet.
*/   
enum class Cls
{
    ZERO,
    NORMAL,
    INF,
    NaN
};

struct Unpacked
{
    bool sign = false;
    int32_t exp = 0;    
    uint32_t sig = 0;   
    Cls cls = Cls::ZERO;
    bool signaling = false;
};

static Unpacked unpack(uint32_t bits)
{
    Unpacked u;
    u.sign = (bits >> 31) & 1;
    const uint32_t E = (bits >> 23) & 0xFF;
    const uint32_t M = bits & 0x7FFFFF;
    // three cases of abnormallity-->
    //case#1--> all exponent bits are 1, i.e. E=255
    // if Mantissa==0 then it is infinity else it is NaN
    if (E == 0xFF)
    {
        u.cls = M ? Cls::NaN : Cls::INF;
        u.signaling = M && !(M & QNAN_BIT);
    }
    //case#2--> all exponent bits are 0, i.e. E=0
    // if Mantissa==0 then it is 0 else it is a Denormal number
    else if (E == 0)
    {
        if (M == 0)
            u.cls = Cls::ZERO;
        else
        {
            // denormal: 0.M * 2^-126 -> shift left until the leading 1 is at bit 23
            u.cls = Cls::NORMAL;
            u.exp = E_MIN;
            u.sig = M;
            while (!(u.sig & (1u << 23)))
            {
                u.sig <<= 1;
                u.exp = isub(u.exp, 1);
            }
        }
    }
    //case#3 ordinary noral number
    else
    {
        u.cls = Cls::NORMAL;
        u.exp = isub(static_cast<int32_t>(E), BIAS); // X = E - bias
        u.sig = M | (1u << 23);                       // P = 1.M
    }
    return u;
}

static uint32_t quiet(uint32_t nanBits) { return nanBits | QNAN_BIT; }

// ---------------- rounding (slides: lsb, round bit r, sticky bit s) ----------------
static bool needIncrement(RoundingMode mode, bool sign, bool lsb, bool r, bool s)
{
    switch (mode)
    {
    case RoundingMode::NEAREST_EVEN:
        return r && (s || lsb); // R > 0.5, or R == 0.5 and lsb(P) == 1
    case RoundingMode::TOWARD_ZERO:
        return false;
    case RoundingMode::TOWARD_POS_INF:
        return !sign && (r || s); // R > 0, positive result
    case RoundingMode::TOWARD_NEG_INF:
        return sign && (r || s); // R > 0, negative result
    }
    return false;
}

// Turns  (-1)^sign * m * 2^(e62 - 62)  into IEEE bits.
//   m   : any non-zero value < 2^63; its extra low bits act as residue (the lowest
//         bit must already contain the sticky OR of anything that was lost earlier)
//   e62 : the exponent that bit 62 of m would have
// Does: normalise, handle denormals, round, renormalise, overflow check.
static uint32_t roundPack(bool sign, int32_t e62, uint64_t m, RoundingMode mode, fpuResult &f)
{
    // 1. normalise: leading 1 to bit 62
    const int p = leadingBit(m);
    const int sh = 62 - p;
    m <<= sh;
    e62 = isub(e62, sh);

    // 2. gradual underflow: below 2^-126 the exponent is frozen and the significand slides right
    const bool tiny = e62 < E_MIN;
    bool tinyAfterRounding = tiny;
    if (tiny)
    {
        // IEEE "tininess after rounding": rounding at full precision may bring us back to 2^-126
        const uint64_t w = m >> 39;
        const bool r = (m >> 38) & 1;
        const bool s = (m & ((1ULL << 38) - 1)) != 0;
        if (e62 == E_MIN - 1 && w == 0xFFFFFF && needIncrement(mode, sign, true, r, s))
            tinyAfterRounding = false;

        m = shiftRightSticky(m, isub(E_MIN, e62));
        e62 = E_MIN;
    }

    // 3. split into 24-bit significand P and residue R (round bit r, sticky bit s)
    uint64_t P = m >> 39;
    const bool r = (m >> 38) & 1;
    const bool s = (m & ((1ULL << 38) - 1)) != 0;
    const bool inexact = r || s;

    if (needIncrement(mode, sign, P & 1, r, s))
        P = add64(P, 1);

    // 4. renormalise after rounding (P may have become 1.0 * 2^24)
    if (P == (1ULL << 24))
    {
        P >>= 1;
        e62 = iadd(e62, 1);
    }

    // 5. overflow
    if (e62 > E_MAX)
    {
        f.flag_OVERFLOW = true;
        f.flag_INEXACT = true;
        const bool toInf = mode == RoundingMode::NEAREST_EVEN ||
                           (mode == RoundingMode::TOWARD_POS_INF && !sign) ||
                           (mode == RoundingMode::TOWARD_NEG_INF && sign);
        return (sign ? SIGN_MASK : 0u) | (toInf ? POS_INF : MAX_FINITE);
    }

    // 6. pack: E = X + bias  (E = 0 for denormals, i.e. when the leading 1 is missing)
    const uint32_t E = (P >> 23) ? static_cast<uint32_t>(iadd(e62, BIAS)) : 0u;
    if (inexact)
    {
        f.flag_INEXACT = true;
        if (tinyAfterRounding)
            f.flag_UNDERFLOW = true;
    }
    return (sign ? SIGN_MASK : 0u) | (E << 23) | static_cast<uint32_t>(P & 0x7FFFFF);
}

// Common NaN handling for two-operand ops. Returns true (and fills `out`) if a NaN is involved.
static bool handleNaN(const Unpacked &ua, const Unpacked &ub, uint32_t a, uint32_t b, uint32_t &out, fpuResult &f)
{
    if (ua.cls != Cls::NaN && ub.cls != Cls::NaN)
        return false;
    if (ua.signaling || ub.signaling)
        f.flag_INVALID = true;
    out = quiet(ua.cls == Cls::NaN ? a : b);
    return true;
}

// 24 x 24 -> 48 bit unsigned multiply, shift-and-add (slides: multiplication flowchart)
static uint64_t mul24(uint32_t x, uint32_t y)
{
    uint64_t acc = 0;
    for (int i = 0; i < 24; ++i)
        if ((y >> i) & 1)
            acc = add64(acc, static_cast<uint64_t>(x) << i);
    return acc;
}

// Unsigned restoring long division (slides: division flowchart).
// Returns floor(num / den); remainder in `rem`.  den < 2^40, num < 2^63.
static uint64_t divide(uint64_t num, uint64_t den, uint64_t &rem)
{
    uint64_t U = 0, Q = 0;
    for (int i = 62; i >= 0; --i)
    {
        U = (U << 1) | ((num >> i) & 1); // shift left (A,Q)
        if (geq64(U, den))               // A - M >= 0 ?
        {
            U = sub64(U, den);
            Q |= (1ULL << i); // Q0 <- 1
        }
    }
    rem = U;
    return Q;
}

// Digit-by-digit (restoring) integer square root: floor(sqrt(N)), remainder in `rem`.
static uint64_t isqrt(uint64_t N, uint64_t &rem)
{
    uint64_t res = 0, R = 0;
    for (int i = 31; i >= 0; --i)
    {
        R = (R << 2) | ((N >> (2 * i)) & 3); // bring down next two bits
        const uint64_t trial = (res << 2) | 1;
        if (geq64(R, trial))
        {
            R = sub64(R, trial);
            res = (res << 1) | 1;
        }
        else
            res <<= 1;
    }
    rem = R;
    return res;
}

// ============================================================================
//  The operations on raw bit patterns
// ============================================================================
static uint32_t fpAdd(uint32_t a, uint32_t b, bool subtract, RoundingMode mode, fpuResult &f)
{
    if (subtract)
        b ^= SIGN_MASK; // a - b = a + (-b)

    const Unpacked ua = unpack(a), ub = unpack(b);
    uint32_t out;
    if (handleNaN(ua, ub, a, b, out, f))
        return out;

    // infinities
    if (ua.cls == Cls::INF || ub.cls == Cls::INF)
    {
        if (ua.cls == Cls::INF && ub.cls == Cls::INF && ua.sign != ub.sign)
        {
            f.flag_INVALID = true; // inf - inf
            return DEFAULT_NAN;
        }
        return ua.cls == Cls::INF ? a : b;
    }

    // zeros
    if (ua.cls == Cls::ZERO && ub.cls == Cls::ZERO)
    {
        if (ua.sign == ub.sign)
            return a;
        return mode == RoundingMode::TOWARD_NEG_INF ? SIGN_MASK : 0u;
    }
    if (ua.cls == Cls::ZERO)
        return b;
    if (ub.cls == Cls::ZERO)
        return a;

    // Step 1: make A the operand with the larger magnitude (so E_A >= E_B)
    Unpacked big = ua, small = ub;
    if (ua.exp < ub.exp || (ua.exp == ub.exp && ua.sig < ub.sig))
    {
        big = ub;
        small = ua;
    }

    // Step 2: align. significands are placed at bit 61..38 so there are 38 guard bits
    uint64_t mBig = static_cast<uint64_t>(big.sig) << 38;
    uint64_t mSmall = static_cast<uint64_t>(small.sig) << 38;
    mSmall = shiftRightSticky(mSmall, isub(big.exp, small.exp)); // W >> (E_A - E_B)

    // Step 3: add or subtract the significands
    const uint64_t sum = (big.sign == small.sign) ? add64(mBig, mSmall) : sub64(mBig, mSmall);

    // exact cancellation
    if (sum == 0)
        return mode == RoundingMode::TOWARD_NEG_INF ? SIGN_MASK : 0u;

    // Step 4: normalise, round (inside roundPack). Bit 61 carries exponent big.exp -> bit 62: +1
    return roundPack(big.sign, iadd(big.exp, 1), sum, mode, f);
}

static uint32_t fpMul(uint32_t a, uint32_t b, RoundingMode mode, fpuResult &f)
{
    const Unpacked ua = unpack(a), ub = unpack(b);
    uint32_t out;
    if (handleNaN(ua, ub, a, b, out, f))
        return out;

    const bool sign = ua.sign != ub.sign;
    const uint32_t signBits = sign ? SIGN_MASK : 0u;

    if ((ua.cls == Cls::INF && ub.cls == Cls::ZERO) || (ua.cls == Cls::ZERO && ub.cls == Cls::INF))
    {
        f.flag_INVALID = true; // 0 * inf
        return DEFAULT_NAN;
    }
    if (ua.cls == Cls::INF || ub.cls == Cls::INF)
        return signBits | POS_INF;
    if (ua.cls == Cls::ZERO || ub.cls == Cls::ZERO)
        return signBits;

    // E = E_A + E_B (bias handled by working with unbiased exponents), W = P_A * P_B
    const uint64_t prod = mul24(ua.sig, ub.sig);
    // value = prod * 2^(Xa + Xb - 46)  ->  exponent of bit 62 is Xa + Xb + 16
    return roundPack(sign, iadd(iadd(ua.exp, ub.exp), 16), prod, mode, f);
}

static uint32_t fpDiv(uint32_t a, uint32_t b, RoundingMode mode, fpuResult &f)
{
    const Unpacked ua = unpack(a), ub = unpack(b);
    uint32_t out;
    if (handleNaN(ua, ub, a, b, out, f))
        return out;

    const bool sign = ua.sign != ub.sign;
    const uint32_t signBits = sign ? SIGN_MASK : 0u;

    if ((ua.cls == Cls::INF && ub.cls == Cls::INF) || (ua.cls == Cls::ZERO && ub.cls == Cls::ZERO))
    {
        f.flag_INVALID = true; // inf/inf, 0/0
        return DEFAULT_NAN;
    }
    if (ua.cls == Cls::INF)
        return signBits | POS_INF;
    if (ub.cls == Cls::INF)
        return signBits; // x / inf = 0
    if (ub.cls == Cls::ZERO)
    {
        f.flag_DIVZERO = true; // x / 0 = inf
        return signBits | POS_INF;
    }
    if (ua.cls == Cls::ZERO)
        return signBits;

    // E = E_A - E_B (unbiased), W = P_A / P_B computed with 30 fractional bits
    uint64_t rem;
    const uint64_t q = divide(static_cast<uint64_t>(ua.sig) << 30, ub.sig, rem);
    const uint64_t m = q | (rem != 0 ? 1ULL : 0ULL); // remainder -> sticky bit
    // value = q * 2^(Xa - Xb - 30)  ->  exponent of bit 62 is Xa - Xb + 32
    return roundPack(sign, iadd(isub(ua.exp, ub.exp), 32), m, mode, f);
}

static uint32_t fpSqrt(uint32_t a, RoundingMode mode, fpuResult &f)
{
    const Unpacked ua = unpack(a);
    if (ua.cls == Cls::NaN)
    {
        if (ua.signaling)
            f.flag_INVALID = true;
        return quiet(a);
    }
    if (ua.cls == Cls::ZERO)
        return a; // sqrt(+-0) = +-0
    if (ua.sign)
    {
        f.flag_INVALID = true; // sqrt of a negative number
        return DEFAULT_NAN;
    }
    if (ua.cls == Cls::INF)
        return a;

    // value = P * 2^t with t = X - 23 ; make t even so the root of 2^t is exact
    int32_t t = isub(ua.exp, 23);
    uint64_t P = ua.sig;
    if (t & 1)
    {
        P <<= 1;
        t = isub(t, 1);
    }
    uint64_t rem;
    const uint64_t root = isqrt(P << 32, rem); // sqrt(P) * 2^16, at least 28 bits
    const uint64_t m = root | (rem != 0 ? 1ULL : 0ULL);
    // value = root * 2^(t/2 - 16)  ->  exponent of bit 62 is t/2 + 46
    return roundPack(false, iadd(t / 2, 46), m, mode, f);
}

static uint32_t fpFromInt(int32_t x, RoundingMode mode, fpuResult &f)
{
    if (x == 0)
        return 0;
    const bool sign = x < 0;
    const uint32_t mag = sign ? neg32(static_cast<uint32_t>(x)) : static_cast<uint32_t>(x); // |x| (INT_MIN -> 2^31)
    return roundPack(sign, 62, mag, mode, f); // value = mag * 2^0 = mag * 2^(62-62)
}

static int32_t fpToInt(uint32_t a, fpuResult &f)
{
    const Unpacked ua = unpack(a);
    if (ua.cls == Cls::NaN)
    {
        f.flag_INVALID = true;
        return 0;
    }
    if (ua.cls == Cls::INF)
    {
        f.flag_INVALID = true;
        return ua.sign ? INT32_MIN : INT32_MAX;
    }
    if (ua.cls == Cls::ZERO)
        return 0;

    if (ua.exp < 0) // |x| < 1 -> truncates to 0
    {
        f.flag_INEXACT = true;
        return 0;
    }
    if (ua.exp >= 31)
    {
        if (ua.sign && ua.exp == 31 && ua.sig == (1u << 23))
            return INT32_MIN; // exactly -2^31 is representable
        f.flag_INVALID = true;
        return ua.sign ? INT32_MIN : INT32_MAX;
    }

    // value = P * 2^(X-23)
    uint32_t mag;
    if (ua.exp >= 23)
        mag = ua.sig << isub(ua.exp, 23);
    else
    {
        const int sh = isub(23, ua.exp);
        if (ua.sig & ((1u << sh) - 1))
            f.flag_INEXACT = true; // fraction bits thrown away
        mag = ua.sig >> sh;
    }
    return static_cast<int32_t>(ua.sign ? neg32(mag) : mag);
}

// Float compare. IEEE ordering equals the ordering of the bit patterns read as
// sign-magnitude integers, so convert to two's complement and reuse the ALU's CMP.
static uint32_t toTwosComplement(uint32_t bits)
{
    return (bits & SIGN_MASK) ? neg32(bits & ~SIGN_MASK) : bits; // -0 becomes 0, so -0 == +0
}

static void fpCompare(uint32_t a, uint32_t b, fpuResult &f)
{
    const Unpacked ua = unpack(a), ub = unpack(b);
    if (ua.cls == Cls::NaN || ub.cls == Cls::NaN)
    {
        f.flag_UNORDERED = true; // NaN is unordered: not equal, not greater
        if (ua.signaling || ub.signaling)
            f.flag_INVALID = true;
        return;
    }
    const aluResult res = fpuALU.execute(aluop::CMP,
                                         static_cast<int32_t>(toTwosComplement(a)),
                                         static_cast<int32_t>(toTwosComplement(b)));
    f.flag_E = res.flag_E;
    f.flag_GT = res.flag_GT;
}


// ============================================================================
//  Public interface
// ============================================================================
fpuResult FPU::add(uint32_t a, uint32_t b)
{
    fpuResult r;
    r.val = static_cast<int32_t>(fpAdd(a, b, false, mode, r));
    return r;
}

fpuResult FPU::sub(uint32_t a, uint32_t b)
{
    fpuResult r;
    r.val = static_cast<int32_t>(fpAdd(a, b, true, mode, r));
    return r;
}

fpuResult FPU::mul(uint32_t a, uint32_t b)
{
    fpuResult r;
    r.val = static_cast<int32_t>(fpMul(a, b, mode, r));
    return r;
}

fpuResult FPU::div(uint32_t a, uint32_t b)
{
    fpuResult r;
    r.val = static_cast<int32_t>(fpDiv(a, b, mode, r));
    return r;
}

fpuResult FPU::sqrt(uint32_t a)
{
    fpuResult r;
    r.val = static_cast<int32_t>(fpSqrt(a, mode, r));
    return r;
}

fpuResult FPU::intToFloat(int32_t a)
{
    fpuResult r;
    r.val = static_cast<int32_t>(fpFromInt(a, mode, r));
    return r;
}

fpuResult FPU::floatToInt(uint32_t a)
{
    fpuResult r;
    r.val = fpToInt(a, r);
    return r;
}

fpuResult FPU::compare(uint32_t a, uint32_t b)
{
    fpuResult r;
    fpCompare(a, b, r);
    return r;
}

fpuResult FPU::execute(fpuop op, int32_t A, int32_t B)
{
    const uint32_t a = static_cast<uint32_t>(A), b = static_cast<uint32_t>(B);
    switch (op)
    {
    case fpuop::FADD:
        return add(a, b);
    case fpuop::FSUB:
        return sub(a, b);
    case fpuop::FMUL:
        return mul(a, b);
    case fpuop::FDIV:
        return div(a, b);
    case fpuop::FSQRT:
        return sqrt(a);
    case fpuop::ITOF:
        return intToFloat(A);
    case fpuop::FTOI:
        return floatToInt(a);
    case fpuop::FCMP:
        return compare(a, b);
    }
    return fpuResult{};
}

float FPU::bitsToFloat(int32_t bits)
{
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

int32_t FPU::floatToBits(float f)
{
    int32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    return bits;
}

std::string FPU::describe(int32_t bits)
{
    const uint32_t u = static_cast<uint32_t>(bits);
    char buf[96];
    std::snprintf(buf, sizeof buf, "S=%u E=%u M=0x%06X  (%g)",
                  u >> 31, (u >> 23) & 0xFF, u & 0x7FFFFF, static_cast<double>(bitsToFloat(bits)));
    return buf;
}