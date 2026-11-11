// ============================================================================
//  tracer.cpp  -  cycle-by-cycle trace exporter for visualizer.html
//
//  Runs a program on the real simulator (PipelinedCore<4> and PipelinedCore<6>, each
//  hardwired / horizontal-micro / vertical-micro) and writes one JSON snapshot per cycle:
//  pipeline slots, registers, flags, memory writes, micro-words, ALU / FPU activity,
//  hazard events and per-stage latency. It does not change how the simulator runs; it only
//  calls PipelinedCore::step() and reads the state afterwards.
//
//  Build  : ./build_trace.sh                      (everything except main.cpp + this file)
//  Usage  : ./simtrace prog.asm [-o trace.json] [--html visualizer.html]
//             --html <template>  also writes a self-contained page with the trace embedded
//             --only pipe4-hmicro  trace one configuration only
// ============================================================================
#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "assembler.h"
#include "cpu.h"
#include "disassembler.h"
#include "pipeline.h"
#include "preprocessor.h"

bool writesRegister(const Instruction &inst, int reg); // defined (non-static) in pipeline.cpp

// ---------------------------------------------------------------- tiny JSON writer
struct JW
{
    std::ostream &o;
    struct L { char close; bool first; };
    std::vector<L> st;
    bool key = false;
    explicit JW(std::ostream &os) : o(os) {}
    void pre()
    {
        if (key) { key = false; return; }
        if (!st.empty()) { if (!st.back().first) o << ','; st.back().first = false; }
    }
    JW &obj() { pre(); o << '{'; st.push_back({'}', true}); return *this; }
    JW &arr() { pre(); o << '['; st.push_back({']', true}); return *this; }
    JW &end() { o << st.back().close; st.pop_back(); return *this; }
    JW &k(const char *name) { pre(); str(name); o << ':'; key = true; return *this; }
    JW &i(long long v) { pre(); o << v; return *this; }
    JW &d(double v) { pre(); char t[40]; std::snprintf(t, sizeof t, "%.4f", v); o << t; return *this; }
    JW &b(bool v) { pre(); o << (v ? "true" : "false"); return *this; }
    JW &null() { pre(); o << "null"; return *this; }
    JW &s(const std::string &v) { pre(); str(v); return *this; }
    void str(const std::string &v)
    {
        o << '"';
        for (unsigned char c : v)
        {
            switch (c)
            {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", c); o << buf; }
                else o << c;
            }
        }
        o << '"';
    }
};

// ---------------------------------------------------------------- names
static const char *OP_NAMES[30] = {
    "add", "sub", "mul", "div", "mod", "cmp", "and", "or", "not", "mov", "lsl", "lsr", "asr", "nop", "ld", "st",
    "beq", "bgt", "b", "call", "ret", "land", "lor", "lnot", "bland", "blor", "blnot", "itof", "ftoi", "sqrtf"};

static const char *CTRL_NAMES[36] = {
    "IR_EN", "FLAG_E_EN", "FLAG_GT_EN", "I_EN", "RD_EN", "RS1_EN", "RS2_EN", "BRANCH_TARGET_EN", "IMMX_EN",
    "FLAG_LAND_EN", "FLAG_LOR_EN", "FLAG_LNOT_EN", "PC_READ", "PC_WRITE", "REG_SRC_READ", "REG_SRC_WRITE",
    "REG_DATA_READ", "REG_DATA_WRITE", "REG_VAL_READ", "REG_VAL_WRITE", "A_READ", "A_WRITE", "B_READ", "B_WRITE",
    "ALU_RESULT_READ", "ALU_RESULT_WRITE", "MAR_READ", "MAR_WRITE", "MDR_READ", "MDR_WRITE", "LD_RESULT_READ",
    "LD_RESULT_WRITE", "TRANSFER_MUX_0", "TRANSFER_MUX_1", "FETCH_MUX_0", "FETCH_MUX_1"};
static_assert(CONTROL_BITS == 36, "control word width changed: update CTRL_NAMES");

static const char *aluName(aluop op)
{
    switch (op)
    {
    case aluop::ADD: return "ADD";
    case aluop::SUB: return "SUB";
    case aluop::MUL: return "MUL";
    case aluop::DIV: return "DIV";
    case aluop::MOD: return "MOD";
    case aluop::CMP: return "CMP";
    case aluop::AND: return "AND";
    case aluop::OR: return "OR";
    case aluop::NOT: return "NOT";
    case aluop::MOV: return "MOV";
    case aluop::LSL: return "LSL";
    case aluop::LSR: return "LSR";
    case aluop::ASR: return "ASR";
    case aluop::LAND: return "LAND";
    case aluop::LOR: return "LOR";
    case aluop::LNOT: return "LNOT";
    }
    return nullptr;
}

static const char *fpuName(fpuop op)
{
    switch (op)
    {
    case fpuop::FADD: return "FADD";
    case fpuop::FSUB: return "FSUB";
    case fpuop::FMUL: return "FMUL";
    case fpuop::FDIV: return "FDIV";
    case fpuop::FSQRT: return "FSQRT";
    case fpuop::ITOF: return "ITOF";
    case fpuop::FTOI: return "FTOI";
    case fpuop::FCMP: return "FCMP";
    }
    return "?";
}

static const char *roundName(RoundingMode m)
{
    switch (m)
    {
    case RoundingMode::NEAREST_EVEN: return "NEAREST_EVEN";
    case RoundingMode::TOWARD_ZERO: return "TOWARD_ZERO";
    case RoundingMode::TOWARD_POS_INF: return "TOWARD_POS_INF";
    case RoundingMode::TOWARD_NEG_INF: return "TOWARD_NEG_INF";
    }
    return "?";
}

// How an micro-word's args field reads: an aluop name, or the FPU function for itof / ftoi / sqrtf.
static std::string argsName(unsigned args, bool hasFn)
{
    if (!hasFn) return "";
    if (const char *n = aluName(static_cast<aluop>(args))) return n;
    if (args >= 27 && args <= 29) return std::string("FPU:") + OP_NAMES[args];
    return "fn#" + std::to_string(args);
}

// ---------------------------------------------------------------- program / source map
struct SrcInfo
{
    std::vector<std::string> labels;
    std::string comment;
};

// Same comment / whitespace stripping as the assembler, so the instruction numbering matches.
static std::string stripLine(std::string line, bool &inBlock)
{
    if (inBlock)
    {
        size_t e = line.find("*/");
        if (e == std::string::npos) return "";
        line.erase(0, e + 2);
        inBlock = false;
    }
    size_t st = line.find("/*");
    while (st != std::string::npos)
    {
        size_t e = line.find("*/", st + 2);
        if (e != std::string::npos) line.erase(st, e - st + 2);
        else { line.erase(st); inBlock = true; break; }
        st = line.find("/*");
    }
    size_t at = line.find('@');
    if (at != std::string::npos) line.erase(at);
    size_t f = line.find_first_not_of(" \t\r\n");
    if (f == std::string::npos) return "";
    size_t l = line.find_last_not_of(" \t\r\n");
    return line.substr(f, l - f + 1);
}

static std::string trim(const std::string &s)
{
    size_t f = s.find_first_not_of(" \t\r\n");
    if (f == std::string::npos) return "";
    return s.substr(f, s.find_last_not_of(" \t\r\n") - f + 1);
}

static std::vector<SrcInfo> mapSource(const std::string &expanded)
{
    std::vector<SrcInfo> out;
    std::istringstream ss(expanded);
    std::string raw;
    bool inBlock = false;
    std::vector<std::string> pending;
    while (std::getline(ss, raw))
    {
        std::string s = stripLine(raw, inBlock);
        if (s.empty()) continue;
        size_t c = s.find(':');
        if (c != std::string::npos)
        {
            pending.push_back(trim(s.substr(0, c)));
            s = trim(s.substr(c + 1));
        }
        if (!s.empty())
        {
            SrcInfo si;
            si.labels = pending;
            size_t at = raw.find('@');
            if (at != std::string::npos) si.comment = trim(raw.substr(at + 1));
            out.push_back(si);
            pending.clear();
        }
    }
    return out;
}

struct Program
{
    std::string name;
    std::vector<std::string> hex, dis;
    std::vector<SrcInfo> src;
};

// ---------------------------------------------------------------- per-instruction helpers
// Copy of getSources() in pipeline.cpp (it is file-static there): which registers an instruction reads.
static void srcRegs(const Instruction &inst, int &a, int &b, int &st)
{
    a = b = st = -1;
    switch (inst.op)
    {
    case Opcode::add: case Opcode::sub: case Opcode::mul: case Opcode::div: case Opcode::mod: case Opcode::cmp:
    case Opcode::and_op: case Opcode::or_op: case Opcode::lsl: case Opcode::lsr: case Opcode::asr:
    case Opcode::land: case Opcode::lor:
        a = inst.rs1;
        if (!inst.isImm) b = inst.rs2;
        break;
    case Opcode::lnot: case Opcode::ld:
        a = inst.rs1;
        break;
    case Opcode::not_op: case Opcode::mov:
        if (!inst.isImm) b = inst.rs2;
        break;
    case Opcode::st:
        a = inst.rs1;
        st = inst.rd;
        break;
    case Opcode::ret:
        a = ra;
        break;
    default:
        break;
    }
}

static bool isBranchOp(Opcode op)
{
    return op == Opcode::b || op == Opcode::beq || op == Opcode::bgt || op == Opcode::call || op == Opcode::ret ||
           op == Opcode::bland || op == Opcode::blor || op == Opcode::blnot;
}

struct ExInfo
{
    std::string unit = "-"; // ALU | FPU | BRANCH | -
    std::string fn;
    std::string note;
    bool hasRes = false;   // result is written to the slot's result field in EX
    uint32_t A = 0, B = 0, val = 0, hi = 0;
    bool hasHi = false;
    bool taken = false;
    uint32_t target = 0;
    aluResult ar{};
    fpuResult fr{};
    bool unsupported = false;
};

struct Flags { bool e, gt, land, lor, lnot; };

static ExInfo computeEx(const Instruction &inst, uint32_t pc, uint32_t a, uint32_t b, const Flags &fl, RoundingMode rm)
{
    ExInfo x;
    x.A = a;
    x.B = b;
    ALU alu;
    FPU fpu;
    fpu.mode = rm;
    auto runAlu = [&](aluop op, const char *note = "")
    {
        x.unit = "ALU";
        x.fn = aluName(op);
        x.note = note;
        x.ar = alu.execute(op, (int32_t)a, (int32_t)b);
        x.val = (uint32_t)x.ar.val;
    };
    auto runFpu = [&](fpuop op)
    {
        x.unit = "FPU";
        x.fn = fpuName(op);
        x.fr = fpu.execute(op, (int32_t)a, (int32_t)b);
        x.val = (uint32_t)x.fr.val;
    };

    if (isBranchOp(inst.op))
    {
        x.unit = "BRANCH";
        x.fn = OP_NAMES[(int)inst.op];
        switch (inst.op)
        {
        case Opcode::b: case Opcode::call: x.taken = true; break;
        case Opcode::beq: x.taken = fl.e; break;
        case Opcode::bgt: x.taken = fl.gt; break;
        case Opcode::bland: x.taken = fl.land; break;
        case Opcode::blor: x.taken = fl.lor; break;
        case Opcode::blnot: x.taken = fl.lnot; break;
        case Opcode::ret: x.taken = true; break;
        default: break;
        }
        x.target = inst.op == Opcode::ret ? a : pc + (uint32_t)(inst.imm << 2);
        return x;
    }

    if (inst.isFloat)
    {
        switch (inst.op)
        {
        case Opcode::add: runFpu(fpuop::FADD); x.hasRes = true; break;
        case Opcode::sub: runFpu(fpuop::FSUB); x.hasRes = true; break;
        case Opcode::mul: runFpu(fpuop::FMUL); x.hasRes = true; break;
        case Opcode::div: runFpu(fpuop::FDIV); x.hasRes = true; break;
        case Opcode::cmp: runFpu(fpuop::FCMP); break;
        default: break;
        }
        return x;
    }

    switch (inst.op)
    {
    case Opcode::add: runAlu(aluop::ADD); x.hasRes = true; break;
    case Opcode::sub: runAlu(aluop::SUB); x.hasRes = true; break;
    case Opcode::mul: runAlu(aluop::MUL); x.hasRes = true; x.hasHi = true; x.hi = (uint32_t)x.ar.hi; break;
    case Opcode::div: runAlu(aluop::DIV); x.hasRes = !x.ar.flag_ERR; break;
    case Opcode::mod: runAlu(aluop::MOD); x.hasRes = !x.ar.flag_ERR; break;
    case Opcode::cmp: runAlu(aluop::CMP); break;
    case Opcode::and_op: runAlu(aluop::AND); x.hasRes = true; break;
    case Opcode::or_op: runAlu(aluop::OR); x.hasRes = true; break;
    case Opcode::not_op: runAlu(aluop::NOT); x.hasRes = true; break;
    case Opcode::mov: runAlu(aluop::MOV); x.hasRes = true; break;
    case Opcode::lsl: runAlu(aluop::LSL); x.hasRes = true; break;
    case Opcode::lsr: runAlu(aluop::LSR); x.hasRes = true; break;
    case Opcode::asr: runAlu(aluop::ASR); x.hasRes = true; break;
    case Opcode::ld: runAlu(aluop::ADD, "effective address = A + imm"); x.hasRes = true; break;
    case Opcode::st: runAlu(aluop::ADD, "effective address = A + imm"); x.hasRes = true; break;
    case Opcode::land: runAlu(aluop::LAND); break;
    case Opcode::lor: runAlu(aluop::LOR); break;
    case Opcode::lnot: runAlu(aluop::LNOT); break;
    default:
        break;
    }
    if (inst.op == Opcode::itof || inst.op == Opcode::ftoi || inst.op == Opcode::sqrt)
    {
        x.unit = "FPU";
        x.fn = inst.op == Opcode::itof ? "ITOF" : inst.op == Opcode::ftoi ? "FTOI" : "FSQRT";
        x.unsupported = true;
        x.note = "no EX datapath for this opcode in pipeline.cpp";
    }
    return x;
}

// ---------------------------------------------------------------- exporter
struct TraceExporter
{
    template <size_t N>
    static void run(JW &w, const Program &prog, int mode, int &selfChecked, int &selfBad);
};

template <size_t N>
void TraceExporter::run(JW &w, const Program &prog, int mode, int &selfChecked, int &selfBad)
{
    using Core = PipelinedCore<N>;
    using Slot = typename Core::Slot;

    CPU cpu;
    cpu.loadHex(prog.hex);
    Core core;
    core.isMicroprogrammed = mode > 0;
    core.useVertical = (mode == 2);
    core.reset();

    const int exIdx = core.exStage - 1, readIdx = core.readStage - 1, writeIdx = core.writeStage - 1;
    const int memStg = core.memStage; // 1-based, as used by the hazard rule
    const Microprogramming &hrom = core.hROM;

    std::vector<std::string> names = N == 4 ? std::vector<std::string>{"IF", "OF", "EX", "MA/RW"}
                                            : std::vector<std::string>{"IF", "OF", "EX", "MA1", "MA2", "RW"};
    std::vector<std::vector<int>> stepsOf(N); // 0 = EX, 1 = MA, 2 = RW
    for (size_t k = 0; k < N; ++k)
    {
        int s = (int)k + 1;
        if (s == core.exStage) stepsOf[k].push_back(Microprogramming::STEP_EX);
        if (s == core.marStage || s == core.memStage) stepsOf[k].push_back(Microprogramming::STEP_MA);
        if (s == core.writeStage) stepsOf[k].push_back(Microprogramming::STEP_RW);
    }

    const char *modeName = mode == 0 ? "hardwired" : mode == 1 ? "horizontal" : "vertical";
    w.obj();
    w.k("stages").i((long long)N).k("mode").s(modeName);
    w.k("names").arr();
    for (auto &n : names) w.s(n);
    w.end();
    w.k("stageSteps").arr();
    for (size_t k = 0; k < N; ++k)
    {
        w.arr();
        for (int st : stepsOf[k]) w.i(st);
        w.end();
    }
    w.end();
    w.k("indices").obj().k("read").i(readIdx).k("ex").i(exIdx).k("write").i(writeIdx).end();
    w.k("fpuMode").s(roundName(cpu.fpu.mode));

    w.k("initial").obj();
    w.k("regs").arr();
    for (int r = 0; r < 16; ++r) w.i((uint32_t)cpu.regs[r]);
    w.end();
    w.end();

    // ---- cycle loop
    w.k("cycles").arr();

    std::array<Slot, N> pre;
    std::vector<int> preIds(N, -1);
    int nextId = 0;
    struct Inst { int id; uint32_t pc; int fetch; int end; std::string fate; };
    std::vector<Inst> insts;
    std::vector<uint8_t> prevMem = cpu.dataMem;
    struct Expect { bool has = false; uint32_t res = 0; int id = -1; } expect;
    std::string lastErr;
    bool truncated = false;
    const unsigned MAX_CYCLES = 20000;

    while (!core.isDone(cpu))
    {
        if (core.cycle_count >= MAX_CYCLES) { truncated = true; break; }

        for (size_t k = 0; k < N; ++k) pre[k] = core.stage[k];
        const uint32_t preStall = core.stall_count, preFlush = core.flush_count, preInst = core.instruction_count;
        const Flags preFl{cpu.flag_E, cpu.flag_GT, cpu.flag_land, cpu.flag_lor, cpu.flag_lnot};

        std::ostringstream outCap, errCap;
        auto *oc = std::cout.rdbuf(outCap.rdbuf());
        auto *ec = std::cerr.rdbuf(errCap.rdbuf());
        core.step(cpu);
        std::cout.rdbuf(oc);
        std::cerr.rdbuf(ec);
        const std::string errText = trim(errCap.str());
        if (!errText.empty()) lastErr = errText;
        const int cycle = (int)core.cycle_count;
        const bool stall = core.stall_count != preStall;
        const uint32_t flushed = core.flush_count - preFlush;

        // ---- instruction identities
        std::vector<int> ids(N, -1);
        for (size_t k = 0; k < N; ++k)
        {
            const Slot &sl = core.stage[k];
            if (!sl.valid) continue;
            int cand = -1;
            if (k == 0)
            {
                if (stall && pre[0].valid && pre[0].pc == sl.pc) cand = preIds[0];
            }
            else if (stall && (int)k <= readIdx)
            {
                if (pre[k].valid && pre[k].pc == sl.pc) cand = preIds[k];
            }
            else if (pre[k - 1].valid && pre[k - 1].pc == sl.pc)
                cand = preIds[k - 1];
            if (cand < 0)
            {
                cand = nextId++;
                insts.push_back({cand, sl.pc, cycle, cycle, ""});
            }
            ids[k] = cand;
            insts[cand].end = cycle;
        }
        for (size_t k = 0; k < N; ++k)
        {
            int id = preIds[k];
            if (id < 0 || std::find(ids.begin(), ids.end(), id) != ids.end()) continue;
            insts[id].fate = !errText.empty() ? "fault" : ((int)k == writeIdx ? "retired" : "flushed");
        }

        // ---- self-check: result computed in EX last cycle vs. what the simulator latched
        if (expect.id >= 0 && errText.empty() && exIdx + 1 < (int)N && core.stage[exIdx + 1].valid &&
            ids[exIdx + 1] == expect.id && expect.has)
        {
            ++selfChecked;
            if (core.stage[exIdx + 1].result != expect.res) ++selfBad;
        }
        expect = Expect{};

        // ---- memory writes (word granularity)
        std::vector<std::pair<uint32_t, uint32_t>> memW;
        for (uint32_t a = 0; a + 3 < cpu.dataMem.size(); a += 4)
            if (!std::equal(cpu.dataMem.begin() + a, cpu.dataMem.begin() + a + 4, prevMem.begin() + a))
            {
                uint32_t v = cpu.dataMem[a] | (cpu.dataMem[a + 1] << 8) | (cpu.dataMem[a + 2] << 16) |
                             ((uint32_t)cpu.dataMem[a + 3] << 24);
                memW.push_back({a, v});
            }
        prevMem = cpu.dataMem;

        // ---- write the cycle
        w.obj();
        w.k("n").i(cycle);
        w.k("pc").i(cpu.pc);
        w.k("flags").obj().k("E").b(cpu.flag_E).k("GT").b(cpu.flag_GT).k("LAND").b(cpu.flag_land)
            .k("LOR").b(cpu.flag_lor).k("LNOT").b(cpu.flag_lnot).end();
        w.k("regs").arr();
        for (int r = 0; r < 16; ++r) w.i((uint32_t)cpu.regs[r]);
        w.end();
        w.k("mem").arr();
        for (auto &m : memW) w.arr().i(m.first).i(m.second).end();
        w.end();

        w.k("slots").arr();
        for (size_t k = 0; k < N; ++k)
        {
            const Slot &sl = core.stage[k];
            w.obj();
            if (!sl.valid) { w.k("v").i(0).end(); continue; }
            w.k("v").i(1).k("id").i(ids[k]).k("i").i(sl.pc / 4).k("pc").i(sl.pc);
            w.k("a").i(sl.a).k("b").i(sl.b).k("st").i(sl.storeVal).k("res").i(sl.result);
            w.k("mar").i(sl.mar).k("mdr").i(sl.mdr).k("fault").b((bool)sl.fault);
            if (mode > 0 && !stepsOf[k].empty())
            {
                w.k("mw").arr();
                try
                {
                    for (int st : stepsOf[k]) w.i((long long)hrom.microPCFor(sl.inst.op, st));
                }
                catch (...) {}
                w.end();
            }
            w.end();
        }
        w.end();

        // ---- ALU / FPU / branch unit: what the EX stage does with the instruction now sitting in it
        w.k("ex");
        if (core.stage[exIdx].valid)
        {
            const Slot &sl = core.stage[exIdx];
            const Flags nowFl{cpu.flag_E, cpu.flag_GT, cpu.flag_land, cpu.flag_lor, cpu.flag_lnot};
            ExInfo x = computeEx(sl.inst, sl.pc, sl.a, sl.b, nowFl, cpu.fpu.mode);
            expect.has = x.hasRes && !x.fr.flag_DIVZERO && !x.fr.flag_INVALID && !x.ar.flag_ERR;
            expect.res = x.val;
            expect.id = ids[exIdx];
            w.obj().k("id").i(ids[exIdx]).k("unit").s(x.unit).k("fn").s(x.fn).k("note").s(x.note);
            w.k("A").i(x.A).k("B").i(x.B).k("val").i(x.val).k("hasRes").b(x.hasRes);
            if (x.hasHi) w.k("hi").i(x.hi);
            if (x.unit == "ALU")
                w.k("flags").obj().k("E").b(x.ar.flag_E).k("GT").b(x.ar.flag_GT).k("LAND").b(x.ar.flag_LAND)
                    .k("LOR").b(x.ar.flag_LOR).k("LNOT").b(x.ar.flag_LNOT).k("ERR").b(x.ar.flag_ERR).end();
            if (x.unit == "FPU" && !x.unsupported)
                w.k("fpu").obj().k("INVALID").b(x.fr.flag_INVALID).k("DIVZERO").b(x.fr.flag_DIVZERO)
                    .k("OVERFLOW").b(x.fr.flag_OVERFLOW).k("UNDERFLOW").b(x.fr.flag_UNDERFLOW)
                    .k("INEXACT").b(x.fr.flag_INEXACT).k("E").b(x.fr.flag_E).k("GT").b(x.fr.flag_GT)
                    .k("UNORDERED").b(x.fr.flag_UNORDERED).end();
            if (x.unit == "BRANCH") w.k("taken").b(x.taken).k("target").i(x.target);
            if (x.unsupported) w.k("unsupported").b(true);
            w.end();
        }
        else
            w.null();

        // ---- events observed during this step
        w.k("events").arr();
        auto ev = [&](const char *type, const std::string &msg)
        { w.obj().k("t").s(type).k("m").s(msg).end(); };
        auto asmOf = [&](const Slot &sl) { return formatInstruction(sl.inst); };
        if (stall)
        {
            std::string why = "load-use hazard";
            if (core.stage[readIdx].valid)
            {
                int sa, sb, ss;
                srcRegs(core.stage[readIdx].inst, sa, sb, ss);
                for (int reg : {sa, sb, ss})
                {
                    if (reg < 0) continue;
                    for (int p = core.exStage; p < core.writeStage; ++p)
                        if (core.stage[p].valid && writesRegister(core.stage[p].inst, reg))
                        {
                            if (core.stage[p].inst.op == Opcode::ld && p < memStg)
                                why = "load-use hazard: r" + std::to_string(reg) + " is still being loaded by \"" +
                                      asmOf(core.stage[p]) + "\" (" + names[p] + "); \"" +
                                      asmOf(core.stage[readIdx]) + "\" waits in OF";
                            break;
                        }
                }
            }
            ev("stall", why);
        }
        if (core.stage[exIdx].valid && !stall)
        {
            const Slot &c = core.stage[exIdx];
            int sa, sb, ss;
            srcRegs(c.inst, sa, sb, ss);
            const char *opn[3] = {"A", "B", "store data"};
            int regsrc[3] = {sa, sb, ss};
            for (int q = 0; q < 3; ++q)
            {
                if (regsrc[q] < 0) continue;
                for (int p = core.exStage; p < core.writeStage; ++p)
                    if (core.stage[p].valid && writesRegister(core.stage[p].inst, regsrc[q]))
                    {
                        ev("fwd", "forwarding r" + std::to_string(regsrc[q]) + " from \"" + asmOf(core.stage[p]) + "\" (" +
                                      names[p] + ") into operand " + opn[q] + " of \"" + asmOf(c) + "\"");
                        break;
                    }
            }
        }
        if (pre[exIdx].valid && isBranchOp(pre[exIdx].inst.op) && errText.empty())
        {
            ExInfo bx = computeEx(pre[exIdx].inst, pre[exIdx].pc, pre[exIdx].a, pre[exIdx].b, preFl, cpu.fpu.mode);
            char buf[32];
            std::snprintf(buf, sizeof buf, "0x%x", bx.target);
            ev("branch", "\"" + asmOf(pre[exIdx]) + "\" resolved in EX: " +
                              (bx.taken ? std::string("TAKEN -> ") + buf : std::string("not taken")) +
                              (bx.taken && flushed ? "; " + std::to_string(flushed) + " younger instruction(s) flushed" : ""));
        }
        if (pre[writeIdx].valid && errText.empty())
            ev("retire", "\"" + asmOf(pre[writeIdx]) + "\" retired");
        if (!errText.empty()) ev("exception", errText);
        w.end();

        w.k("stall").b(stall).k("flushed").i(flushed).k("retired").i(core.instruction_count - preInst);
        w.k("insts").i(core.instruction_count).k("stalls").i(core.stall_count).k("flushes").i(core.flush_count);
        w.k("lat").arr();
        for (size_t k = 0; k < N; ++k) w.i(core.step_latency_ns[k]);
        w.end();
        w.end();

        preIds = ids;
    }
    w.end(); // cycles

    for (auto &in : insts)
        if (in.fate.empty()) in.fate = "in-flight";
    w.k("instances").arr();
    for (auto &in : insts) w.obj().k("id").i(in.id).k("i").i(in.pc / 4).k("fetch").i(in.fetch).k("end").i(in.end).k("fate").s(in.fate).end();
    w.end();

    w.k("summary").obj();
    w.k("cycles").i(core.cycle_count).k("instructions").i(core.instruction_count).k("stalls").i(core.stall_count)
        .k("flushes").i(core.flush_count);
    w.k("cpi").d(core.instruction_count ? (double)core.cycle_count / core.instruction_count : 0.0);
    w.end();
    w.k("truncated").b(truncated);
    w.k("exception");
    if (lastErr.empty()) w.null(); else w.s(lastErr);
    w.k("latencyTotalNs").arr();
    for (size_t k = 0; k < N; ++k) w.i(core.total_latency_ns[k]);
    w.end();
    w.end();
}

// ---------------------------------------------------------------- main
static std::string readFile(const std::string &p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char **argv)
{
    std::string asmPath, outPath, htmlTemplate, only;
    for (int a = 1; a < argc; ++a)
    {
        std::string s = argv[a];
        if (s == "-o" && a + 1 < argc) outPath = argv[++a];
        else if (s == "--html" && a + 1 < argc) htmlTemplate = argv[++a];
        else if (s == "--only" && a + 1 < argc) only = argv[++a];
        else asmPath = s;
    }
    if (asmPath.empty())
    {
        std::cerr << "usage: simtrace prog.asm [-o trace.json] [--html visualizer.html] [--only pipe4-hmicro]\n";
        return 1;
    }
    std::ifstream in(asmPath);
    if (!in) { std::cerr << "cannot open " << asmPath << "\n"; return 1; }
    std::stringstream buf;
    buf << in.rdbuf();
    const std::string source = buf.str();

    std::vector<std::string> errors;
    const std::string expanded = expandMacros(source, errors);
    Program prog;
    prog.hex = assemble(expanded, errors);
    if (!errors.empty())
    {
        for (auto &e : errors) std::cerr << e << "\n";
        return 1;
    }
    prog.dis = disassembleProgram(prog.hex);
    prog.src = mapSource(expanded);
    if (prog.src.size() != prog.hex.size()) prog.src.assign(prog.hex.size(), SrcInfo{});
    size_t slash = asmPath.find_last_of("/\\");
    prog.name = asmPath.substr(slash == std::string::npos ? 0 : slash + 1);

    std::ostringstream js;
    JW w(js);
    w.obj();
    w.k("format").i(1);
    w.k("program").obj();
    w.k("name").s(prog.name);
    w.k("words").arr();
    for (size_t i = 0; i < prog.hex.size(); ++i)
    {
        w.obj().k("pc").i(i * 4).k("hex").s(prog.hex[i]).k("asm").s(prog.dis[i]);
        w.k("labels").arr();
        for (auto &l : prog.src[i].labels) w.s(l);
        w.end();
        w.k("comment").s(prog.src[i].comment).end();
    }
    w.end();
    w.k("source").arr();
    {
        std::istringstream ss(source);
        std::string l;
        while (std::getline(ss, l)) w.s(l);
    }
    w.end();
    w.end();

    w.k("isa").obj().k("opNames").arr();
    for (auto n : OP_NAMES) w.s(n);
    w.end().k("ctrlNames").arr();
    for (auto n : CTRL_NAMES) w.s(n);
    w.end().end();

    w.k("machine").obj().k("dataMemBytes").i(cap_of_data_mem).k("stackBase").i(stack_base).k("stackLimit").i(stack_limit)
        .k("sp").i(sp).k("ra").i(ra).k("instMemWords").i(cap_of_inst_mem).end();

    // ---- control store (horizontal). The vertical store decodes to the same words for every instruction
    // (addresses >= 3, checked here); its front-end words (0..1) are empty placeholders, exported as romV.
    {
        Microprogramming h;
        VerticalMicroprogramming v;
        bool match = h.romSize() == v.romSize();
        auto word = [&](size_t a, const MicroInstruction &m)
        {
            const bool hasFn = m.get(ControlBit::ALU_RESULT_WRITE) || m.get(ControlBit::FLAG_E_EN) ||
                               m.get(ControlBit::FLAG_LAND_EN) || m.get(ControlBit::FLAG_LOR_EN) ||
                               m.get(ControlBit::FLAG_LNOT_EN);
            w.obj().k("addr").i(a).k("label").s(m.label).k("bits").arr();
            for (size_t b = 0; b < CONTROL_BITS; ++b)
                if (m.control[b]) w.i((long long)b);
            w.end();
            w.k("imm").i(m.immediate).k("target").i(m.branchTarget).k("args").i(m.args)
                .k("argName").s(argsName(m.args, hasFn && !m.get(ControlBit::PC_WRITE)))
                .k("fetch").i((int)m.fetchSource());
            if (a >= 3) w.k("op").i((long long)((a - 3) / 3)).k("step").i((long long)((a - 3) % 3));
            w.end();
        };
        w.k("micro").obj();
        w.k("rom").arr();
        for (size_t a = 0; a < h.romSize(); ++a)
        {
            const MicroInstruction &m = h.fetch(a);
            if (match && a >= 3)
            {
                MicroInstruction d = v.fetch(a);
                match = d.control == m.control && d.args == m.args && d.branchTarget == m.branchTarget &&
                        d.fetchSource() == m.fetchSource() && d.label == m.label;
            }
            word(a, m);
        }
        w.end();
        w.k("romV").arr();
        for (size_t a = 0; a < 3 && a < v.romSize(); ++a) word(a, v.fetch(a));
        w.end();
        w.k("entry").arr();
        for (int op = 0; op < 30; ++op) w.i((long long)h.entryFor(static_cast<Opcode>(op)));
        w.end();
        w.k("dispatch").i(2).k("wordBits").i(Microprogramming::MICROINSTRUCTION_BITS)
            .k("fields").obj().k("control").i(36).k("immediate").i(12).k("target").i(10).k("args").i(10).end();
        w.k("verticalMatchesHorizontal").b(match);
        w.end();
    }

    // ---- runs
    int selfChecked = 0, selfBad = 0;
    w.k("runs").obj();
    const char *modes[3] = {"hard", "hmicro", "vmicro"};
    for (int depth : {4, 6})
        for (int m = 0; m < 3; ++m)
        {
            std::string key = "pipe" + std::to_string(depth) + "-" + modes[m];
            if (!only.empty() && only != key) continue;
            w.k(key.c_str());
            try
            {
                if (depth == 4) TraceExporter::run<4>(w, prog, m, selfChecked, selfBad);
                else TraceExporter::run<6>(w, prog, m, selfChecked, selfBad);
            }
            catch (const std::exception &e)
            {
                std::cerr << "trace of " << key << " failed: " << e.what() << "\n";
                return 1;
            }
        }
    w.end();
    w.k("selfCheck").obj().k("checked").i(selfChecked).k("mismatches").i(selfBad).end();
    w.end();

    std::string json = js.str();
    if (outPath.empty()) outPath = "trace.json";
    {
        std::ofstream f(outPath, std::ios::binary);
        f << json;
    }
    std::cout << "wrote " << outPath << " (" << json.size() / 1024 << " KiB), self-check " << selfChecked
              << " results compared, " << selfBad << " mismatches\n";

    if (!htmlTemplate.empty())
    {
        std::string page = readFile(htmlTemplate);
        const std::string token = "__TRACE_JSON__";
        size_t p = page.find(token);
        if (page.empty() || p == std::string::npos)
        {
            std::cerr << "template " << htmlTemplate << " missing or has no " << token << " marker\n";
            return 1;
        }
        // keep "</script>" out of the embedded data
        for (size_t q = 0; (q = json.find("</", q)) != std::string::npos; q += 2) json.replace(q, 2, "<\\/");
        page.replace(p, token.size(), json);
        std::string htmlOut = outPath.substr(0, outPath.find_last_of('.')) + ".html";
        std::ofstream f(htmlOut, std::ios::binary);
        f << page;
        std::cout << "wrote " << htmlOut << "\n";
    }
    return 0;
}
