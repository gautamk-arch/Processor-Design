#include "microprogramming.h"
#include "exceptions.h"
#include "alu.h"

#include <algorithm>
#include <limits>
#include <string>
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

    // One micro-word per pipeline step: step 0 = EX, step 1 = MA, step 2 = RW.
    std::vector<MicroInstruction> code;
    code.push_back(make(std::string(baseLabel) + "_ex"));
    code.push_back(make(std::string(baseLabel) + "_ma"));
    code.push_back(make(std::string(baseLabel) + "_rw"));

    // Bits the pipeline stages read:
    //   EX word : ALU_RESULT_WRITE  EX computes aluResult = ALU(args, A, B)
    //             PC_WRITE          EX changes the PC (branch / call / ret)
    //             A_READ + PC_WRITE pc <- A   (ret)
    //             FLAG_*_EN         with PC_WRITE: branch only if that flag is set
    //                               without PC_WRITE: update that flag (cmp / land / lor / lnot)
    //             args              ALU function, as an aluop value
    //   MA word : LD_RESULT_WRITE   load  memory[result]
    //             MAR_WRITE         store memory[result] <- value
    //   RW word : REG_DATA_WRITE    write the result into rd
    auto wire = [&](std::size_t step,
                    std::initializer_list<ControlBit> bits,
                    std::uint16_t args = 0) {
        MicroInstruction& m = code.at(step);
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
            wire(0, {ControlBit::ALU_RESULT_WRITE}, aluFn);   // EX
            wire(2, {ControlBit::REG_DATA_WRITE});            // RW
            break;
        case Opcode::cmp:
            wire(0, {ControlBit::FLAG_E_EN, ControlBit::FLAG_GT_EN}, static_cast<std::uint16_t>(aluop::CMP));
            break;
        case Opcode::nop:
            break;
        case Opcode::ld:
            wire(0, {ControlBit::ALU_RESULT_WRITE}, static_cast<std::uint16_t>(aluop::ADD)); // EX: address
            wire(1, {ControlBit::LD_RESULT_WRITE});                                          // MA: read memory
            wire(2, {ControlBit::REG_DATA_WRITE});                                           // RW: write rd
            break;
        case Opcode::st:
            wire(0, {ControlBit::ALU_RESULT_WRITE}, static_cast<std::uint16_t>(aluop::ADD)); // EX: address
            wire(1, {ControlBit::MAR_WRITE, ControlBit::MDR_WRITE});                         // MA: write memory
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

    // The last word (RW) returns to mloadIR; build() patches the branch target.
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

// micro-PC of (opcode, step) = entry of the opcode + step
std::size_t Microprogramming::microPCFor(Opcode opcode, std::size_t step) const {
    const std::size_t entry = entryFor(opcode);
    if (step >= programs_[static_cast<std::size_t>(opcode)].instructions.size())
        throw std::out_of_range("Pipeline step out of range for microprogram");
    return entry + step;
}

// control word of (opcode, step)
const MicroInstruction& Microprogramming::fetchForStage(Opcode opcode, std::size_t step) const {
    return fetch(microPCFor(opcode, step));
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
            // The micro-PC stays on mswitch; the caller resolves the jump with
            // entryFor(opcode) once the opcode has been decoded from the IR.
            return microPC;
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