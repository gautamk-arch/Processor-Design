#pragma once
#include "isa.h"
#include "microprogramming.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
//grouping mutually exclusive operations into encoded fields
enum class VDataPathCtrl : std::uint8_t { NONE, ALU_TO_REG, MEM_TO_REG, REG_TO_MEM };
enum class VPcCtrl : std::uint8_t { NONE, BRANCH, CALL, RET };
enum class VFlagCtrl : std::uint8_t { 
    NONE, CMP_UPDATE, 
    COND_E, COND_GT, COND_LAND, COND_LOR, COND_LNOT 
};

//verticalmicroinstruction 
struct VerticalMicroInstruction {
    VDataPathCtrl dpCtrl = VDataPathCtrl::NONE;
    VPcCtrl pcCtrl = VPcCtrl::NONE;
    VFlagCtrl flagCtrl = VFlagCtrl::NONE;
    FetchSource fetchSource = FetchSource::NEXT_MICRO_PC;
    std::uint16_t immediate = 0;
    std::uint16_t branchTarget = 0;
    std::uint16_t args = 0;
    std::string label;
};
struct VerticalMicroProgram {
    std::size_t entry = 0;
    std::vector<VerticalMicroInstruction> instructions;
};
class VerticalMicroprogramming {
public:
    VerticalMicroprogramming();
    void reset();

    std::size_t entryFor(Opcode opcode) const;
    //fetchdecode returns the decoded horizontal microinstruction
    MicroInstruction fetchDecoded(std::size_t microPC) const;
    std::size_t nextMicroPC(std::size_t microPC, bool isMBranch = false) const;
    std::size_t romSize() const noexcept;

private:
    std::array<std::size_t, 27> entry_{};
    std::vector<VerticalMicroInstruction> rom_;
    std::array<VerticalMicroProgram, 27> programs_{};
    void build();
    static VerticalMicroInstruction make(
        std::string label,
        FetchSource source = FetchSource::NEXT_MICRO_PC,
        std::uint16_t immediate = 0,
        std::uint16_t args = 0);
    static std::vector<VerticalMicroInstruction> makeInstructionProgram(Opcode opcode);
    
    // The decoder that convert vertical into horizontal
    static MicroInstruction decode(const VerticalMicroInstruction& vmi);
};