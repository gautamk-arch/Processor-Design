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

// translates encoded fields back to flat ControlBits for the pipeline
MicroInstruction VerticalMicroprogramming::decode(const VerticalMicroInstruction& vmi) {
    MicroInstruction mi;
    mi.label = vmi.label;
    mi.setFetchSource(vmi.fetchSource);
    mi.immediate = vmi.immediate;
    mi.branchTarget = vmi.branchTarget;
    mi.args = vmi.args;

    // decoding datapath control
    switch (vmi.dpCtrl) {
        case VDataPathCtrl::ALU_TO_REG:
            mi.set(ControlBit::ALU_RESULT_WRITE);
            mi.set(ControlBit::REG_DATA_WRITE);
            break;
        case VDataPathCtrl::MEM_TO_REG:
            mi.set(ControlBit::ALU_RESULT_WRITE);
            mi.set(ControlBit::LD_RESULT_WRITE);
            mi.set(ControlBit::REG_DATA_WRITE);
            break;
        case VDataPathCtrl::REG_TO_MEM:
            mi.set(ControlBit::ALU_RESULT_WRITE);
            mi.set(ControlBit::MAR_WRITE);
            mi.set(ControlBit::MDR_WRITE);
            break;
        case VDataPathCtrl::NONE: break;
    }

    //decoding pc control
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
        case VPcCtrl::NONE: break;
    }

    //decoding flag control
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
        case VFlagCtrl::NONE: break;
    }

    return mi;
}

std::vector<VerticalMicroInstruction> VerticalMicroprogramming::makeInstructionProgram(Opcode opcode) {
    std::vector<VerticalMicroInstruction> code;
    auto add = [&](const char* label) { code.push_back(make(label)); };
    switch (opcode) {
        case Opcode::add:   add("madd"); break;
        case Opcode::sub:   add("msub"); break;
        case Opcode::mul:   add("mmul"); break;
        case Opcode::div:   add("mdiv"); break;
        case Opcode::mod:   add("mmod"); break;
        case Opcode::cmp:   add("mcmp"); break;
        case Opcode::and_op:add("mand"); break;
        case Opcode::or_op: add("mor"); break;
        case Opcode::not_op:add("mnot"); break;
        case Opcode::mov:   add("mmov"); break;
        case Opcode::lsl:   add("mlsl"); break;
        case Opcode::lsr:   add("mlsr"); break;
        case Opcode::asr:   add("masr"); break;
        case Opcode::nop:   add("mnop"); break;
        case Opcode::ld:    add("mload"); break;
        case Opcode::st:    add("mstore"); break;
        case Opcode::beq:   add("mbranchEqual"); break;
        case Opcode::bgt:   add("mbranchGreater"); break;
        case Opcode::b:     add("mbranch"); break;
        case Opcode::call:  add("mcall"); break;
        case Opcode::ret:   add("mreturn"); break;
        case Opcode::land:  add("mland"); break;
        case Opcode::lor:   add("mlor"); break;
        case Opcode::lnot:  add("mlnot"); break;
        case Opcode::bland: add("mbranchLand"); break;
        case Opcode::blor:  add("mbranchLor"); break;
        case Opcode::blnot: add("mbranchLnot"); break;
        default:
            throw std::invalid_argument("Unknown SimpleRisc opcode");
    }

    auto wire = [&](std::size_t index, VDataPathCtrl dp, VPcCtrl pc, VFlagCtrl flag, std::uint16_t args = 0) {
        VerticalMicroInstruction& m = code.at(index);
        m.dpCtrl = dp;
        m.pcCtrl = pc;
        m.flagCtrl = flag;
        m.args = args & ((1U << 10) - 1U);
    };

    const std::uint16_t aluFn = static_cast<std::uint16_t>(opcode);

    //wiring all 27 opcodes exactly matching horizontal bit signals
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
            wire(0, VDataPathCtrl::ALU_TO_REG, VPcCtrl::NONE, VFlagCtrl::NONE, aluFn);
            break;
        case Opcode::cmp:
            wire(0, VDataPathCtrl::NONE, VPcCtrl::NONE, VFlagCtrl::CMP_UPDATE, static_cast<std::uint16_t>(aluop::CMP));
            break;
        case Opcode::nop:
            // NOP requires no control fields asserted
            break;
        case Opcode::ld:
            wire(0, VDataPathCtrl::MEM_TO_REG, VPcCtrl::NONE, VFlagCtrl::NONE, static_cast<std::uint16_t>(aluop::ADD));
            break;
        case Opcode::st:
            wire(0, VDataPathCtrl::REG_TO_MEM, VPcCtrl::NONE, VFlagCtrl::NONE, static_cast<std::uint16_t>(aluop::ADD));
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

MicroInstruction VerticalMicroprogramming::fetchDecoded(std::size_t microPC) const {
    if (microPC >= rom_.size()) throw std::out_of_range("Micro-PC is outside microprogram ROM");
    return decode(rom_[microPC]); // Pass the microinstruction through the decoder before returning to CPU
}