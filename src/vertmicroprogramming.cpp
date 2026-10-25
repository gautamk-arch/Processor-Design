#include "vertmicroprogramming.h"
#include "exceptions.h"
#include "alu.h"
#include <limits>
#include <utility>

VerticalMicroprogramming::VerticalMicroprogramming() {
    build();
}

void VerticalMicroprogramming::reset() {
    build();
}

VerticalMicroInstruction VerticalMicroprogramming::make(
    std::string label, FetchSource source, std::uint16_t immediate, std::uint16_t args) {
    VerticalMicroInstruction result;
    result.label = std::move(label);
    result.fetchSource = source;
    result.immediate = immediate & ((1U << 12) - 1U);
    result.args = args & ((1U << 10) - 1U);
    return result;
}

// 1.  Translates stage-specific encoded fields back to flat ControlBits
MicroInstruction VerticalMicroprogramming::decode(const VerticalMicroInstruction& vmi) {
    MicroInstruction mi;
    mi.label = vmi.label;
    mi.setFetchSource(vmi.fetchSource);
    mi.immediate = vmi.immediate;
    mi.branchTarget = vmi.branchTarget;
    mi.args = vmi.args;

    // Decoding stage-specific datapath control
    switch (vmi.dpCtrl) {
        case VDataPathCtrl::ALU_COMPUTE:
            mi.set(ControlBit::ALU_RESULT_WRITE);
            break;
        case VDataPathCtrl::MEM_LOAD:
            mi.set(ControlBit::LD_RESULT_WRITE);
            break;
        case VDataPathCtrl::MEM_STORE:
            mi.set(ControlBit::MAR_WRITE);
            mi.set(ControlBit::MDR_WRITE);
            break;
        case VDataPathCtrl::REG_WRITE:
            mi.set(ControlBit::REG_DATA_WRITE);
            break;
        case VDataPathCtrl::NONE:
            break;
    }

    // Decoding PC control (Active in Step 0 / EX)
    switch (vmi.pcCtrl) {
        case VPcCtrl::BRANCH:
            mi.set(ControlBit::BRANCH_TARGET_EN);
            mi.set(ControlBit::PC_WRITE);
            break;
        case VPcCtrl::CALL:
            mi.set(ControlBit::PC_READ);
            mi.set(ControlBit::BRANCH_TARGET_EN);
            mi.set(ControlBit::PC_WRITE);
            break;
        case VPcCtrl::RET:
            mi.set(ControlBit::A_READ);
            mi.set(ControlBit::PC_WRITE);
            break;
        case VPcCtrl::NONE:
            break;
    }

    // Decoding flag control (Active in Step 0 / EX)
    switch (vmi.flagCtrl) {
        case VFlagCtrl::CMP_UPDATE:
            mi.set(ControlBit::FLAG_E_EN);
            mi.set(ControlBit::FLAG_GT_EN);
            break;
        case VFlagCtrl::COND_E:      mi.set(ControlBit::FLAG_E_EN); break;
        case VFlagCtrl::COND_GT:     mi.set(ControlBit::FLAG_GT_EN); break;
        case VFlagCtrl::COND_LAND:   mi.set(ControlBit::FLAG_LAND_EN); break;
        case VFlagCtrl::COND_LOR:    mi.set(ControlBit::FLAG_LOR_EN); break;
        case VFlagCtrl::COND_LNOT:   mi.set(ControlBit::FLAG_LNOT_EN); break;
        case VFlagCtrl::NONE:
            break;
    }

    return mi;
}

// 2. Generates 3 micro-words per opcode (Step 0: EX, Step 1: MA, Step 2: RW)
std::vector<VerticalMicroInstruction> VerticalMicroprogramming::makeInstructionProgram(Opcode opcode) {
    std::vector<VerticalMicroInstruction> code;
    
    const char* baseLabel = "";
    switch (opcode) {
        case Opcode::add:   baseLabel = "madd"; break;
        case Opcode::sub:   baseLabel = "msub"; break;
        case Opcode::mul:   baseLabel = "mmul"; break;
        case Opcode::div:   baseLabel = "mdiv"; break;
        case Opcode::mod:   baseLabel = "mmod"; break;
        case Opcode::cmp:   baseLabel = "mcmp"; break;
        case Opcode::and_op:baseLabel = "mand"; break;
        case Opcode::or_op: baseLabel = "mor"; break;
        case Opcode::not_op:baseLabel = "mnot"; break;
        case Opcode::mov:   baseLabel = "mmov"; break;
        case Opcode::lsl:   baseLabel = "mlsl"; break;
        case Opcode::lsr:   baseLabel = "mlsr"; break;
        case Opcode::asr:   baseLabel = "masr"; break;
        case Opcode::nop:   baseLabel = "mnop"; break;
        case Opcode::ld:    baseLabel = "mload"; break;
        case Opcode::st:    baseLabel = "mstore"; break;
        case Opcode::beq:   baseLabel = "mbranchEqual"; break;
        case Opcode::bgt:   baseLabel = "mbranchGreater"; break;
        case Opcode::b:     baseLabel = "mbranch"; break;
        case Opcode::call:  baseLabel = "mcall"; break;
        case Opcode::ret:   baseLabel = "mreturn"; break;
        case Opcode::land:  baseLabel = "mland"; break;
        case Opcode::lor:   baseLabel = "mlor"; break;
        case Opcode::lnot:  baseLabel = "mlnot"; break;
        case Opcode::bland: baseLabel = "mbranchLand"; break;
        case Opcode::blor:  baseLabel = "mbranchLor"; break;
        case Opcode::blnot: baseLabel = "mbranchLnot"; break;
        default:
            throw std::invalid_argument("Unknown SimpleRisc opcode");
    }

    // Create 3 steps for every instruction: Step 0 (EX), Step 1 (MA), Step 2 (RW)
    code.push_back(make(std::string(baseLabel) + "_ex"));
    code.push_back(make(std::string(baseLabel) + "_ma"));
    code.push_back(make(std::string(baseLabel) + "_rw"));

    auto wire = [&](std::size_t step, VDataPathCtrl dp, VPcCtrl pc, VFlagCtrl flag, std::uint16_t args = 0) {
        VerticalMicroInstruction& m = code.at(step);
        m.dpCtrl = dp;
        m.pcCtrl = pc;
        m.flagCtrl = flag;
        m.args = args & ((1U << 10) - 1U);
    };

    const std::uint16_t aluFn = static_cast<std::uint16_t>(opcode);

    // Wire each opcode across its 3 pipeline steps (0 = EX, 1 = MA, 2 = RW)
    switch (opcode) {
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
            wire(0, VDataPathCtrl::ALU_COMPUTE, VPcCtrl::NONE, VFlagCtrl::NONE, aluFn); // EX
            wire(2, VDataPathCtrl::REG_WRITE,   VPcCtrl::NONE, VFlagCtrl::NONE);        // RW
            break;
        case Opcode::cmp:
            wire(0, VDataPathCtrl::NONE, VPcCtrl::NONE, VFlagCtrl::CMP_UPDATE, static_cast<std::uint16_t>(aluop::CMP));
            break;
        case Opcode::nop:
            break;
        case Opcode::ld:
            wire(0, VDataPathCtrl::ALU_COMPUTE, VPcCtrl::NONE, VFlagCtrl::NONE, static_cast<std::uint16_t>(aluop::ADD)); // EX: addr calc
            wire(1, VDataPathCtrl::MEM_LOAD,    VPcCtrl::NONE, VFlagCtrl::NONE);                                         // MA: load mem
            wire(2, VDataPathCtrl::REG_WRITE,   VPcCtrl::NONE, VFlagCtrl::NONE);                                         // RW: write reg
            break;
        case Opcode::st:
            wire(0, VDataPathCtrl::ALU_COMPUTE, VPcCtrl::NONE, VFlagCtrl::NONE, static_cast<std::uint16_t>(aluop::ADD)); // EX: addr calc
            wire(1, VDataPathCtrl::MEM_STORE,   VPcCtrl::NONE, VFlagCtrl::NONE);                                         // MA: store mem
            break;
        case Opcode::b:
            wire(0, VDataPathCtrl::NONE, VPcCtrl::BRANCH, VFlagCtrl::NONE);
            break;
        case Opcode::beq:
            wire(0, VDataPathCtrl::NONE, VPcCtrl::BRANCH, VFlagCtrl::COND_E);
            break;
        case Opcode::bgt:
            wire(0, VDataPathCtrl::NONE, VPcCtrl::BRANCH, VFlagCtrl::COND_GT);
            break;
        case Opcode::bland:
            wire(0, VDataPathCtrl::NONE, VPcCtrl::BRANCH, VFlagCtrl::COND_LAND);
            break;
        case Opcode::blor:
            wire(0, VDataPathCtrl::NONE, VPcCtrl::BRANCH, VFlagCtrl::COND_LOR);
            break;
        case Opcode::blnot:
            wire(0, VDataPathCtrl::NONE, VPcCtrl::BRANCH, VFlagCtrl::COND_LNOT);
            break;
        case Opcode::call:
            wire(0, VDataPathCtrl::NONE, VPcCtrl::CALL, VFlagCtrl::NONE);
            break;
        case Opcode::ret:
            wire(0, VDataPathCtrl::NONE, VPcCtrl::RET, VFlagCtrl::NONE);
            break;
        case Opcode::land:
            wire(0, VDataPathCtrl::NONE, VPcCtrl::NONE, VFlagCtrl::COND_LAND, static_cast<std::uint16_t>(aluop::LAND));
            break;
        case Opcode::lor:
            wire(0, VDataPathCtrl::NONE, VPcCtrl::NONE, VFlagCtrl::COND_LOR, static_cast<std::uint16_t>(aluop::LOR));
            break;
        case Opcode::lnot:
            wire(0, VDataPathCtrl::NONE, VPcCtrl::NONE, VFlagCtrl::COND_LNOT, static_cast<std::uint16_t>(aluop::LNOT));
            break;
        default:
            break;
    }

    code.back().label += ":endInstruction";
    return code;
}

void VerticalMicroprogramming::build() {
    rom_.clear();
    entry_.fill(0);

    // 1. Setup the prologue / dispatch sequence (matching horizontal ROM layout)
    rom_.push_back(make("beginInstruction"));
    rom_.push_back(make("mfetch"));
    const std::size_t dispatchAddress = rom_.size();
    rom_.push_back(make("mswitch", FetchSource::OPCODE_SWITCH));
    rom_[dispatchAddress].args = static_cast<std::uint16_t>(dispatchAddress);

    // 2. Populate all 27 instruction microprograms (3 micro-words each)
    for (std::size_t i = 0; i < 27; ++i) {
        const Opcode op = static_cast<Opcode>(i);
        VerticalMicroProgram prog;
        prog.entry = rom_.size();
        prog.instructions = makeInstructionProgram(op);

        // At the end of the microprogram (Step 2 / RW), loop back to instruction fetch (0)
        prog.instructions.back().fetchSource = FetchSource::BRANCH_TARGET;
        prog.instructions.back().branchTarget = 0;

        entry_[i] = prog.entry;
        programs_[i] = prog;

        for (const auto& inst : prog.instructions) {
            rom_.push_back(inst);
        }
    }
}

std::size_t VerticalMicroprogramming::entryFor(Opcode opcode) const {
    const auto idx = static_cast<std::size_t>(opcode);
    if (idx >= entry_.size()) {
        throw IllegalInstruction();
    }
    return entry_[idx];
}

// 3. Calculate exact microPC for (opcode, step)
std::size_t VerticalMicroprogramming::microPCFor(Opcode opcode, std::size_t step) const {
    const auto idx = static_cast<std::size_t>(opcode);
    if (idx >= programs_.size()) {
        throw std::invalid_argument("Opcode out of range in VerticalMicroprogramming::microPCFor");
    }
    if (step >= programs_[idx].instructions.size()) {
        throw std::out_of_range("Pipeline step out of range for microprogram");
    }
    return entry_[idx] + step;
}

// 4. Directly fetch and decode the microinstruction for (opcode, step)
MicroInstruction VerticalMicroprogramming::fetchForStage(Opcode opcode, std::size_t step) const {
    return fetchDecoded(microPCFor(opcode, step));
}

std::size_t VerticalMicroprogramming::nextMicroPC(std::size_t microPC, bool isMBranch) const {
    if (microPC >= rom_.size()) {
        throw std::out_of_range("Micro-PC is outside microprogram ROM");
    }
    const auto& current = rom_[microPC];
    switch (current.fetchSource) {
        case FetchSource::NEXT_MICRO_PC:
            return microPC + 1;
        case FetchSource::BRANCH_TARGET:
            return current.branchTarget;
        case FetchSource::CONDITIONAL_BRANCH_RESULT:
            return isMBranch ? current.branchTarget : (microPC + 1);
        case FetchSource::OPCODE_SWITCH:
            return microPC; // Resolved dynamically by CPU using entryFor(opcode)
        default:
            return microPC + 1;
    }
}

std::size_t VerticalMicroprogramming::romSize() const noexcept {
    return rom_.size();
}

MicroInstruction VerticalMicroprogramming::fetchDecoded(std::size_t microPC) const {
    if (microPC >= rom_.size()) {
        throw std::out_of_range("Micro-PC is outside microprogram ROM");
    }
    return decode(rom_[microPC]); // Pass the microinstruction through the decoder before returning to CPU
}