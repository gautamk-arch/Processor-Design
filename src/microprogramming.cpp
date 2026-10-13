#include "microprogramming.h"
#include "exceptions.h"

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
        case Opcode::ld:
            add("mcomputeAddress");
            add("mreadMemory");
            add("mwriteLoadedValue");
            break;
        case Opcode::st:
            add("mcomputeAddress");
            add("mwriteMemory");
            break;
        case Opcode::beq:   add("mbranchEqual"); break;
        case Opcode::bgt:   add("mbranchGreater"); break;
        case Opcode::b:     add("mbranch"); break;
        case Opcode::call:
            add("msaveReturnAddress");
            add("mbranch");
            break;
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
