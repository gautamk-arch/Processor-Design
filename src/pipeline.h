#pragma once
#include <cstddef>
#include <cstdint>
#include <exception>
#include "isa.h"
#include "cpu.h"
#include "microprogramming.h"
#include "vertmicroprogramming.h"

// This struct acts as a shared blueprint for building different CPU pipelines.
//  It uses "pure virtual" functions (=0) which means it doesn't do anything on its own. Instead, it forces the derived struct of pipelines (4-stage and 6-stage) to implement these functions.
struct PipelineBase
{
    size_t currentMicroPC = 0;
    bool isMicroprogrammed = false; // true: control words come from a micro-ROM, false: hardwired
    bool useVertical = false;       // only meaningful when isMicroprogrammed: false = horizontal, true = vertical

    virtual ~PipelineBase() = default;
    virtual void reset() = 0;
    virtual void step(CPU &cpu) = 0;
    virtual void run(CPU &cpu) = 0;
    virtual void printPipelineDiagram() const = 0;
};


// N = number of stages
// N = 4 : IF, OF, EX, MA&RW
// N = 6 : IF, OF, EX, MA1, MA2, RW
template <size_t N>
struct PipelinedCore : PipelineBase
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
        uint32_t mar = 0;
        uint32_t mdr = 0;
        std::exception_ptr fault;
        MicroInstruction uInst;
    };
    Slot stage[N];
    Microprogramming hROM;  // horizontal control store (wide control word)
    VerticalMicroprogramming vROM; // vertical control store (encoded fields + decoder)

    // Stage numbers are 1-based (IF = 1).
    int readStage;  // OF: operands are read here
    int exStage;    // EX: ALU work, branches resolve here
    int marStage;   // MA1: Address is loaded in memory
    int memStage;   // MA2: Load/store takes place in memory
    int writeStage; // RW: results are written into register file

    uint32_t cycle_count = 0;
    uint32_t instruction_count = 0;
    uint32_t stall_count = 0;
    uint32_t flush_count = 0;

    PipelinedCore()
    {
        static_assert(N == 4 || N == 6, "PipelinedCore supports only 4 or 6 stages");
        if (N == 4)
            configure4stage();
        else
            configure6stage();
    }
    void reset() override
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

    // IF, OF, EX, MA&RW 
    void configure4stage()
    {
        readStage = 2;
        exStage = 3;
        marStage = 4;
        memStage = 4;
        writeStage = 4;
    }
    
    // IF, OF, EX, MA1, MA2, RW
    // MA1 models memory latency (address and data are sent to mar and mdr), MA2 models memory access
    void configure6stage()
    {
        readStage = 2;
        exStage = 3;
        marStage = 4;
        memStage = 5;
        writeStage = 6;
    }
    
    bool isDone(const CPU &cpu) const;
    void step(CPU &cpu) override;
    void run(CPU &cpu) override;
    void printPipelineDiagram() const override;
};