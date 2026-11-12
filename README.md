# Academic Context & Team

* **University:** Indian Institute of Technology Roorkee
* **Course:** Computer Organization & Architecture (`CSC-201`) — Autumn 2026
* **Course Instructor:** Prof. Peddoju Sateesh Kumar

### Development Team

| Name | Enrollment No | Github link |
| :--- | :--- | :--- |
| **Adwait Shiva** | `25114005` | [@AdwaitS-A](https://github.com/AdwaitS-A) |
| **Anant Kumar** | `25114012` | [@Anant1607](https://github.com/Anant1607) |
| **Devendra Sahu** | `25114029` | [@sahu236](https://github.com/sahu236) |
| **Gautam Kannapiran** | `25114039` | [@gautamk-arch](https://github.com/gautamk-arch) |
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
* **Interactive Web Pipeline Visualizer (`simtrace` + `trace.html`):**
  * A trace exporter runs the real simulator and records every cycle to a JSON trace, which a standalone HTML page replays with play / step / scrub controls. It shows the current pipeline stage of every instruction, registers and flags, data memory and stack, the active micro-words and control bits ($\mu\text{PC}$ / ROM view), ALU operands and flags, FPU IEEE 754 bit fields and exception flags, an instruction × cycle timeline (stalls and flushes marked), an event log, and per-stage profiling. Supports switching between the 4-stage and 6-stage pipelines and between Hardwired, Horizontal and Vertical control units.

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
./risc201_sim -d examples/factorial.asm
```
Debugger also includes stack visualizer.
For debugger commands, pls refer here [link for debugger.md](docs/debugger_and_exceptions.md)

---

### 5. Launching the Web Pipeline Visualizer

The visualizer is built from `tracer.cpp` (kept in the project root, outside `src/`, because it has its own `main()`) together with the simulator sources in `src/` except `src/main.cpp`. It needs `page.html` / `template.html` from the project root.

#### Build the trace exporter (`simtrace`)
From the RISC201 directory run:
```bash
g++ -std=c++17 -O0 -g -Wl,--stack,268435456 -Isrc tracer.cpp $(ls src/*.cpp | grep -v '/main.cpp$') -o simtrace
```
The larger stack size (`-Wl,--stack`) is needed on Windows (MinGW).

#### Generate and open the visualizer for a program
```bash
./simtrace examples/array_sum.asm --html template.html
start trace.html          # Windows  (use xdg-open on Linux / open on macOS)
```
This runs the program on all 6 pipelined configurations (4-/6-stage × Hardwired / Horizontal / Vertical), writes `trace.json`, and produces a standalone `trace.html` that opens in any browser. Use `-o name.json` to choose the output name (the page is then written as `name.html`) and `--only pipe6-hmicro` to trace a single configuration (`pipe4|pipe6` + `-hard|-hmicro|-vmicro`).

If `template.html` is missing, rebuild it from `page.html`:
```bash
printf '<!doctype html>\n<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"></head><body>\n' > template.html
cat page.html >> template.html
printf '\n</body></html>\n' >> template.html
```

#### Using the page
* Transport bar: first / back / play / forward / last, scrubber, speed, and jump to the previous / next stall or flush; keyboard shortcuts are supported.
* Switch between 4-stage / 6-stage and Hardwired / Horizontal / Vertical µcode at the top of the page.
* Click the dot beside a program row to set a breakpoint, click any cell of the instruction × cycle chart to jump to that cycle, or use **Load trace** (or drag and drop) to open a different `trace.json`.
* Every cycle is the pipeline as the simulator's `pipe` diagram prints it after that step. Profiling times are host wall-clock nanoseconds of the simulator, not modelled hardware delay.

**Limitations:** the Single-Cycle baseline is not covered by the visualizer, and the ALU/FPU panels do not recompute `itof`, `ftoi` and `sqrt` results (shown as unsupported).

---

## Project Structure

```text
risc201-cpu-simulator/
├── docs/                                            
│   ├── images/                                     
│   │   ├── aludiag1.png / aludiag2.png              
│   │   └── pipeline1.jpeg / pipeline2.jpeg          
│   ├── Test Comparisons.xlsx                        
│   ├── arithmetic_logical_unit.md                  
│   ├── assembly_toolchain_preprocessor.md         
│   ├── debugger_and_exceptions.md                   
│   ├── floating_point_unit.md                     
│   ├── horizontal_microprogramming.md              
│   ├── pipelining_and_hazards.md                   
│   ├── risc201_instruction_set_architecture_isa.md 
│   └── vertical_microprogramming.md                 
├── examples/                                      
│   └── array_sum.asm, factorial.asm, fpu_test.asm, stack_overflow.asm, sum_loop.asm, etc.
├── src/                                         
│   ├── alu.h / .cpp                             
│   ├── assembler.h / .cpp                        
│   ├── cpu.h / .cpp                             
│   ├── disassembler.h / .cpp                     
│   ├── exceptions.h / .cpp                     
│   ├── fpu.h / .cpp                             
│   ├── isa.h / .cpp                              
│   ├── main.cpp                     
│   ├── microprogramming.h / .cpp                 
│   ├── pipeline.h / .cpp                     
│   ├── preprocessor.h / .cpp                      
│   └── vertmicroprogramming.h / .cpp               
├── test/                                          
│   └── test_preproc.cpp                          
├── tracer.cpp                                  # visualizer trace exporter (builds simtrace)
├── page.html / template.html                   # visualizer web page (template.html = page.html wrapped as a full document)
├── .gitignore                                      
├── MakeFile                                        
├── README.md                                     
└── simulator.exe

---
```

## Documentation Directory

For an in-depth exploration of the microarchitectural design decisions, encoding formats, hazard resolution tables, and experimental evaluation, please refer to our dedicated technical documentation:

1. *[Instruction Set Architecture (ISA)](docs/risc201_instruction_set_architecture_isa.md)*
2. *[Assembler and Preprocessor](docs/assembly_toolchain_preprocessor.md)*
3. *[ALU](docs/arithmetic_logical_unit.md)*
4. *[FPU](docs/floating_point_unit.md)*
5. *[Horizontal_microprogramming](docs/horizontal_microprogramming.md)*
5. *[Vertical_microprogramming](docs/vertical_microprogramming.md)*
6. *[Pipeline Architecture & Hazard Logic](docs/pipelining_and_hazards.md)*
8. *[Custom Debugger & Exception Handling](docs/debugger_and_exceptions.md)*
