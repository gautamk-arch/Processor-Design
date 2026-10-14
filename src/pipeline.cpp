#include "pipeline.h"
#include <iostream>
#include <iomanip>
#include <stdexcept>
using std::cout;

static const char *opNames[] = {
    "add", "sub", "mul", "div", "mod", "cmp", "and", "or", "not", "mov", "lsl", "lsr", "asr",
    "nop", "ld", "st", "beq", "bgt", "b", "call", "ret", "land", "lor", "lnot", "bland", "blor", "blnot"};

bool writesRegister(const Instruction &inst, int reg)
{
    if (reg < 0)
        return false;
    switch (inst.op)
    {
    case Opcode::add:
    case Opcode::sub:
    case Opcode::mul:
    case Opcode::div:
    case Opcode::mod:
    case Opcode::and_op:
    case Opcode::or_op:
    case Opcode::not_op:
    case Opcode::mov:
    case Opcode::lsl:
    case Opcode::lsr:
    case Opcode::asr:
    case Opcode::ld:
        return inst.rd == reg;
    case Opcode::call:
        return reg == ra;
    default:
        return false;
    }
}

template <size_t N>
bool hasDataHazard(const typename PipelinedCore<N>::Slot *stage, int readStage, int writeStage)
{
    int readIdx = readStage - 1;
    if (!stage[readIdx].valid)
        return false;

    const Instruction &inst = stage[readIdx].inst;
    int r1 = -1, r2 = -1;

    switch (inst.op)
    {
    case Opcode::add:
    case Opcode::sub:
    case Opcode::mul:
    case Opcode::div:
    case Opcode::mod:
    case Opcode::cmp:
    case Opcode::and_op:
    case Opcode::or_op:
    case Opcode::lsl:
    case Opcode::lsr:
    case Opcode::asr:
    case Opcode::land:
    case Opcode::lor:
        r1 = inst.rs1;
        if (!inst.isImm)
            r2 = inst.rs2;
        break;
    case Opcode::lnot:
    case Opcode::ld:
        r1 = inst.rs1;
        break;
    case Opcode::not_op:
    case Opcode::mov:
        if (!inst.isImm)
            r2 = inst.rs2;
        break;
    case Opcode::st:
        r1 = inst.rs1;
        r2 = inst.rd;
        break;
    case Opcode::ret:
        r1 = ra;
        break;
    default:
        break;
    }

    for (int i = readStage; i < writeStage - 1; ++i)
    {
        if (stage[i].valid)
        {
            if ((r1 != -1 && writesRegister(stage[i].inst, r1)) ||
                (r2 != -1 && writesRegister(stage[i].inst, r2)))
                return true;
        }
    }
    return false;
}

template <size_t N>
bool PipelinedCore<N>::isDone(const CPU &cpu) const
{
    if (cpu.pc < cpu.instruction_limit)
        return false;
    for (size_t i = 0; i < N; ++i)
        if (stage[i].valid)
            return false;
    return true;
}

template <size_t N>
void PipelinedCore<N>::run(CPU &cpu)
{
    while (!isDone(cpu))
        step(cpu);
    cout << "Execution stopped.\n";
}

template <size_t N>
void PipelinedCore<N>::step(CPU &cpu)
{
    if (isDone(cpu))
        return;
    bool stall = false;

    try
    {
        for (int i = N - 1; i >= 0; --i)
        {
            int currentStageNum = i + 1;

            // MA / RW stage

            if (currentStageNum == memStage && stage[i].valid)
            {
                Instruction inst = stage[i].inst;
                uint32_t addr = stage[i].result;

                bool executeLoad = isMicroprogrammed ? stage[i].uInst.get(ControlBit::LD_RESULT_WRITE) : (inst.op == Opcode::ld);
                bool executeStore = isMicroprogrammed ? stage[i].uInst.get(ControlBit::MAR_WRITE) : (inst.op == Opcode::st);
                if (executeLoad)
                {
                    cpu.checkMemoryAccess(addr);
                    if (inst.rs1 == sp && static_cast<int32_t>(addr) >= stack_base)
                        throw StackUnderflow();
                    stage[i].result = (cpu.dataMem[addr]) | (cpu.dataMem[addr + 1] << 8) | (cpu.dataMem[addr + 2] << 16) | (cpu.dataMem[addr + 3] << 24);
                }
                else if (executeStore)
                {
                    cpu.checkMemoryAccess(addr);
                    if (inst.rs1 == sp && static_cast<int32_t>(addr) < stack_limit)
                        throw StackOverflow();
                    uint32_t val = stage[i].storeVal;
                    cpu.dataMem[addr] = val & 0xFF;
                    cpu.dataMem[addr + 1] = (val >> 8) & 0xFF;
                    cpu.dataMem[addr + 2] = (val >> 16) & 0xFF;
                    cpu.dataMem[addr + 3] = (val >> 24) & 0xFF;
                }
            }

            if (currentStageNum == writeStage && stage[i].valid)
            {
                Instruction inst = stage[i].inst;
                bool executeWrite = isMicroprogrammed ? stage[i].uInst.get(ControlBit::REG_DATA_WRITE) : writesRegister(inst, inst.rd);
                if (inst.op == Opcode::call)
                    cpu.regs[ra] = stage[i].pc + 4;
                else if (executeWrite)
                {
                    cpu.regs[inst.rd] = static_cast<int32_t>(stage[i].result);
                    if (inst.rd == sp)
                    {
                        if (cpu.regs[sp] < stack_limit)
                            throw StackOverflow();
                        if (cpu.regs[sp] > stack_base)
                            throw StackUnderflow();
                    }
                }
                instruction_count++;
                stage[i].valid = false;
                continue;
            }

            // EX stage 

            if (currentStageNum == exStage && stage[i].valid)
            {
                Instruction inst = stage[i].inst;
                int32_t A = static_cast<int32_t>(stage[i].a), B = static_cast<int32_t>(stage[i].b);
                uint32_t current_pc = stage[i].pc;
                bool branchTaken = false;

                if (isMicroprogrammed)
                {
                    MicroInstruction uInst = stage[i].uInst;

                    if (uInst.get(ControlBit::ALU_RESULT_WRITE))
                    {
                        stage[i].result = cpu.alu.execute(static_cast<aluop>(uInst.args), A, B).val;
                    }
                    if (uInst.get(ControlBit::FLAG_E_EN) || uInst.get(ControlBit::FLAG_GT_EN))
                    {
                        aluResult res = cpu.alu.execute(aluop::CMP, A, B);
                        cpu.flag_E = res.flag_E;
                        cpu.flag_GT = res.flag_GT;
                    }
                    if (uInst.get(ControlBit::PC_WRITE))
                    {
                        bool conditionMet = true;
                        switch (inst.op) {
                            case Opcode::beq:   conditionMet = cpu.flag_E; break;
                            case Opcode::bgt:   conditionMet = cpu.flag_GT; break;
                            case Opcode::bland: conditionMet = cpu.flag_land; break;
                            case Opcode::blor:  conditionMet = cpu.flag_lor; break;
                            case Opcode::blnot: conditionMet = cpu.flag_lnot; break;
                            default:            break; 
                        }
                        if (conditionMet) {
                            if (inst.op == Opcode::ret) {
                                cpu.pc = stage[i].a;
                            }
                            else{
                                cpu.pc = current_pc + (inst.imm << 2);
                            }
                            branchTaken = true;
                        }
                    }
                }
                else
                {
                    switch (inst.op)
                    {
                    case Opcode::add:
                        stage[i].result = cpu.alu.execute(aluop::ADD, A, B).val;
                        break;
                    case Opcode::sub:
                        stage[i].result = cpu.alu.execute(aluop::SUB, A, B).val;
                        break;
                    case Opcode::mul:
                        stage[i].result = cpu.alu.execute(aluop::MUL, A, B).val;
                        break;
                    case Opcode::div:
                    {
                        aluResult res = cpu.alu.execute(aluop::DIV, A, B);
                        if (res.flag_ERR)
                            throw DivideByZero();
                        stage[i].result = res.val;
                        break;
                    }
                    case Opcode::mod:
                    {
                        aluResult res = cpu.alu.execute(aluop::MOD, A, B);
                        if (res.flag_ERR)
                            throw DivideByZero();
                        stage[i].result = res.val;
                        break;
                    }
                    case Opcode::cmp:
                    {
                        aluResult res = cpu.alu.execute(aluop::CMP, A, B);
                        cpu.flag_E = res.flag_E;
                        cpu.flag_GT = res.flag_GT;
                        break;
                    }
                    case Opcode::and_op:
                        stage[i].result = cpu.alu.execute(aluop::AND, A, B).val;
                        break;
                    case Opcode::or_op:
                        stage[i].result = cpu.alu.execute(aluop::OR, A, B).val;
                        break;
                    case Opcode::not_op:
                        stage[i].result = cpu.alu.execute(aluop::NOT, A, B).val;
                        break;
                    case Opcode::mov:
                        stage[i].result = cpu.alu.execute(aluop::MOV, A, B).val;
                        break;
                    case Opcode::lsl:
                        stage[i].result = cpu.alu.execute(aluop::LSL, A, B).val;
                        break;
                    case Opcode::lsr:
                        stage[i].result = cpu.alu.execute(aluop::LSR, A, B).val;
                        break;
                    case Opcode::asr:
                        stage[i].result = cpu.alu.execute(aluop::ASR, A, B).val;
                        break;
                    case Opcode::nop:
                        break;
                    case Opcode::ld:
                        stage[i].result = cpu.alu.execute(aluop::ADD, A, B).val;
                        break;
                    case Opcode::st:
                        stage[i].result = cpu.alu.execute(aluop::ADD, A, ImmValue(inst.mod, inst.imm)).val;
                        break;
                    case Opcode::b:
                    case Opcode::call:
                        cpu.pc = current_pc + (inst.imm << 2);
                        branchTaken = true;
                        break;
                    case Opcode::beq:
                        if (cpu.flag_E)
                        {
                            cpu.pc = current_pc + (inst.imm << 2);
                            branchTaken = true;
                        }
                        break;
                    case Opcode::bgt:
                        if (cpu.flag_GT)
                        {
                            cpu.pc = current_pc + (inst.imm << 2);
                            branchTaken = true;
                        }
                        break;
                    case Opcode::bland:
                        if (cpu.flag_land)
                        {
                            cpu.pc = current_pc + (inst.imm << 2);
                            branchTaken = true;
                        }
                        break;
                    case Opcode::blor:
                        if (cpu.flag_lor)
                        {
                            cpu.pc = current_pc + (inst.imm << 2);
                            branchTaken = true;
                        }
                        break;
                    case Opcode::blnot:
                        if (cpu.flag_lnot)
                        {
                            cpu.pc = current_pc + (inst.imm << 2);
                            branchTaken = true;
                        }
                        break;
                    case Opcode::ret:
                        cpu.pc = stage[i].a;
                        branchTaken = true;
                        break;
                    case Opcode::land:
                        cpu.flag_land = cpu.alu.execute(aluop::LAND, A, B).flag_LAND;
                        break;
                    case Opcode::lor:
                        cpu.flag_lor = cpu.alu.execute(aluop::LOR, A, B).flag_LOR;
                        break;
                    case Opcode::lnot:
                        cpu.flag_lnot = cpu.alu.execute(aluop::LNOT, A, B).flag_LNOT;
                        break;
                    default:
                        throw IllegalInstruction();
                    }
                }

                if (branchTaken)
                {
                    for (int j = 0; j < i; ++j)
                    {
                        if (stage[j].valid)
                        {
                            stage[j].valid = false;
                            flush_count++;
                        }
                    }
                    if (isMicroprogrammed)
                        isMicroSequencerBusy = false;
                }
            }

            // OF stage

            if (currentStageNum == readStage)
            {
                if (hasDataHazard<N>(stage, readStage, writeStage))
                {
                    stall = true;
                    stall_count++;
                }
                else if (stage[i].valid)
                {
                    Instruction inst = stage[i].inst;
                    stage[i].a = (inst.op == Opcode::ret) ? cpu.regs[ra] : cpu.regs[inst.rs1];
                    stage[i].b = inst.isImm ? ImmValue(inst.mod, inst.imm) : cpu.regs[inst.rs2];
                    stage[i].storeVal = (inst.op == Opcode::st) ? cpu.regs[inst.rd] : 0;
                    if (isMicroprogrammed)
                    {
                        if (!isMicroSequencerBusy)
                        {
                            currentMicroPC = microROM.entryFor(inst.op);
                            isMicroSequencerBusy = true;
                        }

                        stage[i].uInst = microROM.fetch(currentMicroPC);

                        if (stage[i].uInst.fetchSource() == FetchSource::NEXT_MICRO_PC)
                        {
                            currentMicroPC++;
                        }
                        else
                        {
                            isMicroSequencerBusy = false;
                        }
                    }
                }
            }

            if (i + 1 < static_cast<int>(N) && (!stall || currentStageNum > readStage))
            {
                // Prevent IF stage from shifting into OF while the micro-sequencer is busy translating.
                bool preventShift = (isMicroprogrammed && isMicroSequencerBusy && currentStageNum == 1);

                if (!preventShift) 
                {
                    stage[i + 1] = stage[i];
                    
                    // Prevent OF stage from invalidating itself during multi-cycle routines.
                    bool keepInOF = (isMicroprogrammed && isMicroSequencerBusy && currentStageNum == readStage);
                    if (!keepInOF)
                    {
                        stage[i].valid = false;
                    }
                }
            }

            // IF stage

            if (currentStageNum == 1 && !stall)
            {
                if (isMicroprogrammed && isMicroSequencerBusy)
                {
                    // Natural stall holding the IF stage state
                }
                else
                {
                    if (cpu.pc < cpu.instruction_limit)
                    {
                        stage[0].valid = true;
                        stage[0].inst = decode(cpu.fetchInst());
                        stage[0].pc = cpu.pc;
                        stage[0].a = 0;
                        stage[0].b = 0;
                        stage[0].result = 0;
                        stage[0].storeVal = 0;
                        if (isMicroprogrammed)
                            stage[0].uInst = MicroInstruction{};
                        cpu.pc += 4;
                    }
                    else
                        stage[0].valid = false;
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "\n------------------------------\n";
        std::cerr << "HARDWARE EXCEPTION: " << e.what() << "\n";
        std::cerr << "Cycle   :" << std::dec << (cycle_count + 1) << "\n";
        std::cerr << "------------------------------\n";
        cpu.pc = cpu.instruction_limit;
        for (size_t i = 0; i < N; ++i)
            stage[i].valid = false;
        if (isMicroprogrammed)
            isMicroSequencerBusy = false;
    }

    cycle_count++;
    printPipelineDiagram();
}

template <size_t N>
void PipelinedCore<N>::printPipelineDiagram() const
{
    const char *labels[4] = {"IF", "OF", "EX", "MA/RW"};
    cout << "Cycle " << std::setw(3) << cycle_count << " | ";
    for (size_t i = 0; i < N; ++i)
    {
        cout << labels[i] << ": ";
        if (stage[i].valid)
            cout << std::setw(5) << opNames[static_cast<int>(stage[i].inst.op)] << " | ";
        else
            cout << "  --- | ";
    }
    double cpi = (instruction_count > 0) ? static_cast<double>(cycle_count) / instruction_count : 0.0;
    cout << "Insts: " << instruction_count << " Stalls: " << stall_count << " Flushes: " << flush_count
         << " CPI: " << std::fixed << std::setprecision(2) << cpi << "\n";
}

template struct PipelinedCore<4>;