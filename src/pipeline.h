#pragma once
#include <cstdint>
#include "isa.h"
#include "cpu.h"
#include "microprogramming.h"
#include "vertmicroprogramming.h"

template <size_t N>
struct PipelinedCore
{
    struct Slot
    {
        bool valid;
        Instruction inst;
        uint32_t pc = 0;
        uint32_t a = 0;
        uint32_t b = 0;
        uint32_t storeVal = 0;
        uint32_t result = 0;
        MicroInstruction uInst;
    };
    Slot stage[N];
    Microprogramming hROM;  // horizontal control store (wide control word)
    VerticalMicroprogramming vROM; // vertical control store (encoded fields + decoder)
    size_t currentMicroPC = 0;
    bool isMicroprogrammed = false;
    bool useVertical = false;

    int readStage;
    int exStage;
    int memStage;
    int writeStage;

    uint32_t cycle_count = 0;
    uint32_t instruction_count = 0;
    uint32_t stall_count = 0;
    uint32_t flush_count = 0;

    PipelinedCore()
    {
        configure4stage();
    }
    void reset()
    {
        for (size_t i = 0; i < N; ++i)
        {
            stage[i] = Slot{};
        }
        cycle_count = 0;
        instruction_count = 0;
        stall_count = 0;
        flush_count = 0;
    }
    void configure4stage()
    {
        readStage = 2;
        exStage = 3;
        memStage = 4;
        writeStage = 4;
    }

    bool isDone(const CPU &cpu) const;
    void step(CPU &cpu);
    void run(CPU &cpu);
    void printPipelineDiagram() const;
};