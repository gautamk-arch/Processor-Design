# Academic Context & Team

* **University:** Indian Institute of Technology Roorkee
* **Course:** Computer Organization & Architecture (`CSC-201`) — Autumn 2026
* **Course Instructor:** Prof. Peddoju Sateesh Kumar

### Development Team

| Name | Enrollment No | Github link |
| :--- | :--- | :--- |
| **Adwait Shiva** | `25114005` | [@AdwaitS-A](https://github.com/AdwaitS-A) |
| **Anant Kumar** | `25114012` | [@Anant1607](https://github.com/Anant1607) |
| **Gautam Kannapiran** | `25114039` | [@gautamk-arch](https://github.com/gautamk-arch) |
| **Devendra Sahu** | `25114029` | [@sahu236](https://github.com/sahu236) |
| **Gunbir Singh** | `25114041` | [@gunbirsingh007-web](https://github.com/gunbirsingh007-web) |

---

# RISC201: Cycle-Accurate Pipelined CPU & Microprogrammed Simulator

**RISC201** is a cycle-accurate, multi-architecture custom CPU simulator and complete compilation toolchain written in modern C++ (`C++23`). It models a Single-Cycle baseline alongside configurable **4-stage** and **6-stage pipelined processors**, featuring dynamic hazard resolution, an integrated **ALU**, **IEEE 754 Floating-Point Unit (FPU)**, and runtime-switchable **Hardwired, Horizontal Microprogrammed, and Vertical Microprogrammed** control units.

---

## Key Features

* **Multi-Depth Pipelined Architecture (`PipelinedCore<4>` & `PipelinedCore<6>`):**
  * **6-Stage Pipeline (`--pipe6`):** Instruction Fetch (**IF**), Operand Fetch (**OF**), Execute (**EX**), Memory Access 1 (**MA1**), Memory Access 2 (**MA2**), and Register Write-Back (**RW**) to accurately model split-cycle data cache access latencies.
  * **4-Stage Pipeline (`--pipe4`):** Classic compacted pipeline (**IF**, **OF**, **EX**, **MA+RW**) for direct architectural IPC and stall penalty comparison against the 6-stage core.
  * **Single-Cycle Baseline:** Non-pipelined reference execution engine for architectural verification.
* **Dynamic Hazard Resolution:**
  * **Data Forwarding (Bypassing):** Full forwarding network from `EX`, `MA1`, `MA2`, and `RW` pipeline latches back to the `EX` stage to eliminate Read-After-Write (RAW) stalls where data is ready.
  * **Load-Use Hazard Interlocks:** Automatic hardware stall injection.
  * **Control Hazard Flushing:** Speculative fetch squashing and pipeline register flushing upon branch/jump resolution in the Execute stage.
* **IEEE 754 Floating-Point Unit (FPU) & Optimized ALU:**
  * Dedicated single-precision ($32\text{-bit}$ IEEE 754) floating-point execution paths supporting `addf`, `subf`, `mulf`, `divf`, hardware square root (`sqrt`), and bidirectional type conversion (`itof` and `ftoi`).
  * Optimized integer ALU supporting arithmetic, bitwise logical, and barrel-shifter operations with hardware flag generation.
* **7 Configurable Execution Modes (Hardwired & Decoupled Microprogramming):**
  * Supports **Hardwired Control** for single-cycle stage decoding, alongside **Horizontal Microprogramming** (wide unencoded control words for maximum parallelism) and **Vertical Microprogramming** (compact encoded $\mu\text{Ops}$ requiring secondary decoding) with real-time $\mu\text{PC}$ tracking.
* **Complete End-to-End Assembler Toolchain:**
  * **Stack Macro Preprocessor:** Expands high-level full-descending stack macros (`PUSH`, `POP`, `CALL`, `RET`) prior to assembly.
  * **Two-Pass Assembler:** Resolves symbolic labels and translates `.asm` source files into machine code (`.hex`) with detailed syntax and bounds error reporting.
  * **Disassembler:** Automatically reconstructs human-readable assembly (`.dis`) from generated hexadecimal machine words.
* **Memory Protection & Active Stack Visualizer:**
  * Runtime hardware exception handling for stack overflow, stack underflow, unaligned accesses, and stack-heap boundary collisions, paired with an interactive ASCII stack memory frame renderer.
* **Integrated CLI Debugger & Nanosecond Stage Latency Profiling:**
  * Interactive debugging shell supporting breakpoints, single-stepping, register/memory inspection, live architecture hot-swapping (`mode` command), ASCII pipeline stage diagrams, and `std::chrono` nanosecond-precision stage latency and control-unit overhead profiling.

---

## Quick Start / How to Run

### 1. Prerequisites
* **Compiler:** GCC (`g++`) with **C++23** support (tested with **MSYS2 MinGW-w64 `g++` 13+** on Windows and `g++` on Linux/macOS).
* **Build System:** direct compilation via `g++` is supported

### 2. Building the Simulator

#### Direct Compilation via `g++` (MSYS2 / Linux)
Compile in terminal while in RISC201 directory run:
```bash
g++ src/*.cpp -o simulator.exe
```

---

### 3. Running the Simulator & Execution Modes

Whenever you pass an `<input_program.asm>` file to the simulator, it automatically:
1. Expands stack preprocessor macros.
2. Runs the two-pass assembler to emit `<input_program>.hex`.
3. Runs the disassembler to emit `<input_program>.dis`.
4. Executes the program on the configured CPU architecture.

#### CLI Syntax
```bash
./risc201_sim [-d|--debug] [--pipe4|--pipe6] [--hmicro|--vmicro] <filename.asm>
```

#### All 7 Hardware Execution Modes
You can launch the simulator in any of the **7 architectural configurations** directly from the terminal (factorial.asm here is example program present in examples folder, user can run the desired assembly program by replacing "examples/factorial.asm" with the path of the desired program.):

```bash
# 1. Single-Cycle Hardwired CPU (Default baseline)
./risc201_sim examples/factorial.asm

# 2. 4-Stage Pipelined — Hardwired Control Unit
./risc201_sim --pipe4 examples/factorial.asm

# 3. 4-Stage Pipelined — Horizontal Microprogrammed Control Unit
./risc201_sim --pipe4 --hmicro examples/factorial.asm

# 4. 4-Stage Pipelined — Vertical Microprogrammed Control Unit
./risc201_sim --pipe4 --vmicro examples/factorial.asm

# 5. 6-Stage Pipelined — Hardwired Control Unit
./risc201_sim --pipe6 examples/factorial.asm

# 6. 6-Stage Pipelined — Horizontal Microprogrammed Control Unit
./risc201_sim --pipe6 --hmicro examples/factorial.asm

# 7. 6-Stage Pipelined — Vertical Microprogrammed Control Unit
./risc201_sim --pipe6 --vmicro examples/factorial.asm
```

---

### 4. Launching the Interactive CLI Debugger

Append `-d` or `--debug` to any command to enter the interactive hardware debugger:
```bash
./risc201_sim -d --pipe6 --hmicro examples/factorial.asm
```
Debugger also includes stack visualizer.
For debugger commands, pls refer here [link for debugger.md](docs/debugger_and_exceptions.md)

---

## Project Structure

```text
risc201-cpu-simulator/
├── main.cpp               # CLI driver, argument parser, toolchain pipeline & interactive debugger loop
├── preprocessor.h/.cpp    # Macro expansion engine for full-descending stack operations
├── assembler.h/.cpp       # Two-pass RISC201 assembler (.asm -> .hex) with symbol table & error reporting
├── disassembler.h/.cpp    # Binary/Hexadecimal instruction parser and assembly reconstructor (.hex -> .dis)
├── cpu.h/.cpp             # Base architectural state, register file, memory protection & ASCII stack visualizer
├── pipeline.h/.cpp        # Template-based PipelinedCore<4> & PipelinedCore<6> reverse-evaluation engine,
│                          # hazard detection unit, forwarding multiplexers, and latency profiler
├── alu.h/.cpp             # Optimized integer Arithmetic Logic Unit (arithmetic, barrel shift, bitwise)
├── fpu.h/.cpp             # IEEE 754 Floating-Point Unit (fadd, fsub, fmul, fdiv, fsqrt, itof, ftoi)
├── control.h/.cpp         # Hardwired decoder, Horizontal & Vertical Control Memory ROMs, and microPC logic
├── Makefile               # Build configuration for C++23 compilation
├── tests/                 # Assembly test suite (ALU, FPU, recursion/stack bounds, hazard stress tests)
└── docs/                  # Deep-dive architectural documentation
    ├── pipeline.md
    ├── isa_fpu.md
    ├── control.md
    └── debugger.md
```

---

## Documentation Directory

For an in-depth exploration of the microarchitectural design decisions, encoding formats, hazard resolution tables, and experimental evaluation, please refer to our dedicated technical documentation:

1. *[Instruction Set Architecture (ISA)](docs/risc201_instruction_set_architecture_isa.md)*
2. *[Assembler and Preprocessor](docs/assembly_toolchain_preprocessor.md)*
3. *[ALU](docs/arithmetic_logical_unit.md)*
4. *[FPU](docs/floating_point_unit.md)*
5. "[Horizontal_microprogramming](docs/horizontal_microprogramming.md)
5. "[Vertical_microprogramming](docs/vertical_microprogramming.md)
6. **[Pipeline Architecture & Hazard Logic](docs/pipelining_and_hazards.md)**
8. **[Custom Debugger & Exception Handling](docs/debugger_and_exceptions.md)**
