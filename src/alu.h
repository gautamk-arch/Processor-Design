#pragma once
#include <cstdint>

enum class aluop{
    ADD, SUB, MUL, DIV, MOD, CMP, AND, OR, NOT, MOV, LSL, LSR, ASR
};

struct aluResult{
    int32_t val = 0;        // main 32-bit result (MUL: low word, MOD: remainder)
    int32_t hi = 0;         // MUL: high 32 bits of the 64-bit product
    bool flag_E = false;    // CMP: A == B
    bool flag_GT = false;   // CMP: A > B (signed)
    bool flag_ERR = false;  // DIV/MOD by zero
};

class ALU{
    public:
    aluResult execute(aluop op, int32_t A, int32_t B);
};