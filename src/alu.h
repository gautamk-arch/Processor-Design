#pragma once
#include <cstdint>

enum class aluop{
    ADD, SUB, MUL, DIV, MOD, CMP, AND, OR, NOT, MOV, LSL, LSR, ASR
};

struct aluResult{
    int32_t val;
    bool flag_E;
    bool flag_GT;
};

class ALU{
    public:
    aluResult execute(aluop op, int32_t A, int32_t B);
};