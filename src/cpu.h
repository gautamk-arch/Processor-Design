#pragma once
#include "isa.h"
#include "exceptions.h"
#include <cstdint>
#include <iostream>
#include <vector>
#include <array>
#include <string>
const int no_of_reg = 16;
const int cap_of_inst_mem = 1024;
const int cap_of_data_mem = 4096;
const int sp = 14;
const int ra = 15;

const int32_t stack_base=cap_of_data_mem;
const int32_t stack_limit=cap_of_data_mem-1024;
class CPU
{
private:
    uint32_t pc;
    std::array<int32_t, no_of_reg> regs;
    bool flag_E;
    bool flag_GT;

    std::vector<uint32_t> instMem;
    std::vector<uint8_t> dataMem;

    uint32_t instruction_limit;

    uint32_t fetchInst();

    void checkMemoryAccess(uint32_t addr) const; //to keep stackguard
public:
    CPU();
    void reset();
    void loadHex(const std::vector<std::string> &hexLines);

    void run();
    void step();
    void execute(Instruction inst);

    void dumpRegisters() const;

    void printStack() const; //ASCII stack visualizer
};
