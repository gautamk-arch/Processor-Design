#pragma once
#include "isa.h"
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

inline constexpr std::size_t CONTROL_BITS = 36;

enum class ControlBit : std::size_t {
    IR_EN = 0, FLAG_E_EN, FLAG_GT_EN, I_EN, RD_EN, RS1_EN, RS2_EN,
    BRANCH_TARGET_EN, IMMX_EN, FLAG_LAND_EN, FLAG_LOR_EN, FLAG_LNOT_EN,
    PC_READ, PC_WRITE,
    REG_SRC_READ, REG_SRC_WRITE,
    REG_DATA_READ, REG_DATA_WRITE,
    REG_VAL_READ, REG_VAL_WRITE,
    A_READ, A_WRITE,
    B_READ, B_WRITE,
    ALU_RESULT_READ, ALU_RESULT_WRITE,
    MAR_READ, MAR_WRITE,
    MDR_READ, MDR_WRITE,
    LD_RESULT_READ, LD_RESULT_WRITE,
    TRANSFER_MUX_0, TRANSFER_MUX_1,
    FETCH_MUX_0, FETCH_MUX_1
};

enum class FetchSource : std::uint8_t {
    NEXT_MICRO_PC = 0,
    BRANCH_TARGET = 1,
    CONDITIONAL_BRANCH_RESULT = 2,
    OPCODE_SWITCH = 3
};

struct MicroInstruction {
    std::bitset<CONTROL_BITS> control{};
    std::uint16_t immediate = 0;
    std::uint16_t branchTarget = 0;
    std::uint16_t args = 0;
    std::string label;

    void set(ControlBit bit, bool value = true);
    bool get(ControlBit bit) const;
    void setFetchSource(FetchSource source);
    FetchSource fetchSource() const;
};

struct MicroProgram {
    std::size_t entry = 0;
    std::vector<MicroInstruction> instructions;
};

class Microprogramming {
public:
    static constexpr std::size_t CONTROL_BITS = 36;
    static constexpr std::size_t IMMEDIATE_BITS = 12;
    static constexpr std::size_t BRANCH_TARGET_BITS = 10;
    static constexpr std::size_t ARGS_BITS = 10;
    static constexpr std::size_t MICROINSTRUCTION_BITS =
        CONTROL_BITS + IMMEDIATE_BITS + BRANCH_TARGET_BITS + ARGS_BITS;
    static constexpr std::size_t MICROPROGRAM_SIZE = 1024;

    Microprogramming();
    void reset();

    std::size_t entryFor(Opcode opcode) const;
    const MicroInstruction& fetch(std::size_t microPC) const;
    std::size_t nextMicroPC(std::size_t microPC,
                            bool isMBranch = false) const;
    const MicroProgram& programFor(Opcode opcode) const;
    std::size_t romSize() const noexcept;

    static Opcode opcodeOf(const Instruction& inst) noexcept;

private:
    std::array<std::size_t, 27> entry_{};
    std::vector<MicroInstruction> rom_;
    std::array<MicroProgram, 27> programs_{};

    void build();
    static MicroInstruction make(
        std::string label,
        FetchSource source = FetchSource::NEXT_MICRO_PC,
        std::uint16_t immediate = 0,
        std::uint16_t args = 0);
    static std::vector<MicroInstruction> makeInstructionProgram(
        Opcode opcode);
};
