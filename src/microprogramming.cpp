#include "microprogramming.h"
#include "exceptions.h"
#include "alu.h"

#include <algorithm>
#include <limits>
#include <utility>

void MicroInstruction::set(ControlBit bit, bool value) {
    control.set(static_cast<std::size_t>(bit), value);
}

bool MicroInstruction::get(ControlBit bit) const {
    return control.test(static_cast<std::size_t>(bit));
}

void MicroInstruction::setFetchSource(FetchSource source) {
    const auto value = static_cast<std::uint8_t>(source);
    set(ControlBit::FETCH_MUX_0, value & 1U);
    set(ControlBit::FETCH_MUX_1, value & 2U);
}

FetchSource MicroInstruction::fetchSource() const {
    const unsigned value =
        (get(ControlBit::FETCH_MUX_0) ? 1U : 0U) |
        (get(ControlBit::FETCH_MUX_1) ? 2U : 0U);
    return static_cast<FetchSource>(value);
}

Microprogramming::Microprogramming() {
    build();
}

void Microprogramming::reset() {
    build();
}

MicroInstruction Microprogramming::make(std::string label,
                                        FetchSource source,
                                        std::uint16_t immediate,
                                        std::uint16_t args) {
    MicroInstruction result;
    result.label = std::move(label);
    result.setFetchSource(source);
    result.immediate = immediate & ((1U << IMMEDIATE_BITS) - 1U);
    result.args = args & ((1U << ARGS_BITS) - 1U);
    return result;
}

std::vector<MicroInstruction>
Microprogramming::makeInstructionProgram(Opcode opcode) {
    // These are control-flow templates, not yet a complete electrical-level
    // datapath microcode. They preserve the ISA operations present in cpu.cpp.
    // The datapath-specific register enables / ALU command encoding need to be
    // wired once the project's shared-bus datapath contract is finalized.
    std::vector<MicroInstruction> code;
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

    // ---- Control words for the pipelined core (ONE word per ISA instruction) ----
    // The pipeline stages read these bits:
    //   ALU_RESULT_WRITE : EX computes aluResult = ALU(args, A, B)
    //   REG_DATA_WRITE   : MA/RW writes the result into rd
    //   LD_RESULT_WRITE  : MA/RW loads memory[result]
    //   MAR_WRITE        : MA/RW stores B to memory[result]   (EX uses imm as operand B)
    //   PC_WRITE         : EX changes the PC (branch / call / ret)
    //   A_READ + PC_WRITE: pc <- A   (ret)
    //   FLAG_*_EN        : with PC_WRITE: branch only if that flag is set
    //                      without PC_WRITE: update that flag (cmp / land / lor / lnot)
    //   args             : ALU function, as an aluop value
    auto wire = [&](std::size_t index,
                    std::initializer_list<ControlBit> bits,
                    std::uint16_t args = 0) {
        MicroInstruction& m = code.at(index);
        for (ControlBit bit : bits) m.set(bit);
        m.args = args & ((1U << ARGS_BITS) - 1U);
    };
    const std::uint16_t aluFn = static_cast<std::uint16_t>(opcode);   // add..asr: ISA opcode == aluop

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
            wire(0, {ControlBit::ALU_RESULT_WRITE, ControlBit::REG_DATA_WRITE}, aluFn);
            break;
        case Opcode::cmp:
            wire(0, {ControlBit::FLAG_E_EN, ControlBit::FLAG_GT_EN}, static_cast<std::uint16_t>(aluop::CMP));
            break;
        case Opcode::nop:
            break;
        case Opcode::ld:
            wire(0, {ControlBit::ALU_RESULT_WRITE, ControlBit::LD_RESULT_WRITE,
                     ControlBit::REG_DATA_WRITE}, static_cast<std::uint16_t>(aluop::ADD));
            break;
        case Opcode::st:
            wire(0, {ControlBit::ALU_RESULT_WRITE, ControlBit::MAR_WRITE, ControlBit::MDR_WRITE},
                 static_cast<std::uint16_t>(aluop::ADD));
            break;
        case Opcode::b:
            wire(0, {ControlBit::BRANCH_TARGET_EN, ControlBit::PC_WRITE});
            break;
        case Opcode::beq:
            wire(0, {ControlBit::BRANCH_TARGET_EN, ControlBit::PC_WRITE, ControlBit::FLAG_E_EN});
            break;
        case Opcode::bgt:
            wire(0, {ControlBit::BRANCH_TARGET_EN, ControlBit::PC_WRITE, ControlBit::FLAG_GT_EN});
            break;
        case Opcode::bland:
            wire(0, {ControlBit::BRANCH_TARGET_EN, ControlBit::PC_WRITE, ControlBit::FLAG_LAND_EN});
            break;
        case Opcode::blor:
            wire(0, {ControlBit::BRANCH_TARGET_EN, ControlBit::PC_WRITE, ControlBit::FLAG_LOR_EN});
            break;
        case Opcode::blnot:
            wire(0, {ControlBit::BRANCH_TARGET_EN, ControlBit::PC_WRITE, ControlBit::FLAG_LNOT_EN});
            break;
        case Opcode::call:
            wire(0, {ControlBit::PC_READ, ControlBit::BRANCH_TARGET_EN, ControlBit::PC_WRITE});
            break;
        case Opcode::ret:
            wire(0, {ControlBit::A_READ, ControlBit::PC_WRITE});
            break;
        case Opcode::land:
            wire(0, {ControlBit::FLAG_LAND_EN}, static_cast<std::uint16_t>(aluop::LAND));
            break;
        case Opcode::lor:
            wire(0, {ControlBit::FLAG_LOR_EN}, static_cast<std::uint16_t>(aluop::LOR));
            break;
        case Opcode::lnot:
            wire(0, {ControlBit::FLAG_LNOT_EN}, static_cast<std::uint16_t>(aluop::LNOT));
            break;
        default:
            break;
    }

    // Last microinstruction returns to the next ISA instruction's fetch entry.
    // The address is patched in build() after the common fetch sequence is laid out.
    code.back().label += ":endInstruction";
    return code;
}

void Microprogramming::build() {
    rom_.clear();
    entry_.fill(std::numeric_limits<std::size_t>::max());

    // Common front-end sequence, corresponding conceptually to mloadIR,
    // mdecode, mswitch in the book. The opcode-switch step dispatches to the
    // selected instruction's microprogram entry.
    rom_.push_back(make("mloadIR"));
    rom_.push_back(make("mdecode"));
    rom_.push_back(make("mswitch", FetchSource::OPCODE_SWITCH));
    const std::size_t dispatchAddress = 2;

    // mloadIR: IR <- instruction memory[PC]
    rom_[0].set(ControlBit::PC_READ);
    rom_[0].set(ControlBit::IR_EN);
    // mdecode: extract fields, read register file, latch A and B,
    // sign-extend immediate and compute branch target
    rom_[1].set(ControlBit::RS1_EN);
    rom_[1].set(ControlBit::RS2_EN);
    rom_[1].set(ControlBit::RD_EN);
    rom_[1].set(ControlBit::I_EN);
    rom_[1].set(ControlBit::IMMX_EN);
    rom_[1].set(ControlBit::BRANCH_TARGET_EN);
    rom_[1].set(ControlBit::REG_SRC_READ);
    rom_[1].set(ControlBit::REG_DATA_READ);
    rom_[1].set(ControlBit::A_WRITE);
    rom_[1].set(ControlBit::B_WRITE);

    for (std::size_t raw = 0; raw < programs_.size(); ++raw) {
        const Opcode opcode = static_cast<Opcode>(raw);
        auto body = makeInstructionProgram(opcode);
        // At the end of an ISA instruction, return to mloadIR (address 0).
        // The 10-bit branch target stores microinstruction indices, not bytes.
        body.back().setFetchSource(FetchSource::BRANCH_TARGET);
        body.back().branchTarget = 0;
        programs_[raw].entry = rom_.size();
        programs_[raw].instructions = body;
        entry_[raw] = programs_[raw].entry;
        rom_.insert(rom_.end(), body.begin(), body.end());
    }

    // Store the dispatch address in args for trace/debug consumers. The real
    // switch unit selects entry_[opcode] based on the opcode from IR.
    rom_[dispatchAddress].args = static_cast<std::uint16_t>(dispatchAddress);

    // Each instruction body's last microinstruction branches back to mloadIR
    // at address 0, so the next ISA instruction can be fetched.
}

std::size_t Microprogramming::entryFor(Opcode opcode) const {
    const auto raw = static_cast<std::size_t>(opcode);
    if (raw >= entry_.size() || entry_[raw] == std::numeric_limits<std::size_t>::max())
        throw IllegalInstruction();   // unused opcode (27..31) or not built
    return entry_[raw];
}

const MicroInstruction& Microprogramming::fetch(std::size_t microPC) const {
    if (microPC >= rom_.size())
        throw std::out_of_range("Micro-PC is outside microprogram ROM");
    return rom_[microPC];
}

std::size_t Microprogramming::nextMicroPC(std::size_t microPC, bool isMBranch) const {
    const auto& current = fetch(microPC);
    switch (current.fetchSource()) {
        case FetchSource::NEXT_MICRO_PC:
            return microPC + 1;
        case FetchSource::BRANCH_TARGET:
            return current.branchTarget;
        case FetchSource::CONDITIONAL_BRANCH_RESULT:
            return isMBranch ? current.branchTarget : microPC + 1;
        case FetchSource::OPCODE_SWITCH:
            // mswitch requires opcode-based dispatch; use entryFor(opcode) once
            // the caller has decoded the opcode from the IR register.
            throw std::logic_error("OPCODE_SWITCH needs opcode dispatch; call entryFor(opcode)");
    }
    throw std::logic_error("Invalid micro-fetch mux selection");
}

const MicroProgram& Microprogramming::programFor(Opcode opcode) const {
    const auto raw = static_cast<std::size_t>(opcode);
    if (raw >= programs_.size()) throw std::out_of_range("Unknown opcode");
    return programs_[raw];
}

std::size_t Microprogramming::romSize() const noexcept {
    return rom_.size();
}

Opcode Microprogramming::opcodeOf(const Instruction& inst) noexcept {
    return inst.op;
}