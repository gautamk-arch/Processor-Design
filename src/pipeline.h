#pragma once
#include <cstdint>
#include "isa.h"

template <size_t N>
struct PipelinedCore{
        struct Slot{
            bool valid;
            Instruction inst;
            uint32_t pc;
            uint32_t a;
            uint32_t b;
            uint32_t result;
        };
        Slot stage[N];
        int readStage;
        int exStage;
        int memStage;
        int writeStage;

        uint32_t cycle_count=0;
        uint32_t instruction_count=0;
        uint32_t stall_count=0;
        uint32_t flush_count=0;

        void configure4stage(){
            readStage=2;
            exStage=3;
            memStage=4;
            writeStage=4;
        }

};