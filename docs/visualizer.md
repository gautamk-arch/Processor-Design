# Pipeline Visualizer

## 1. Purpose

The simulator's debugger shows one cycle of text at a time. That is enough to find a bug, but not enough to *see* a processor working: which instruction is in which stage, why a stall happened, which control word is driving the datapath, what the ALU computed. The **Pipeline Visualizer** replays any program cycle by cycle in a web page, so every one of those questions can be answered by looking at the screen.

The visualizer is **trace driven**. A small C++ program (`simtrace`, built from `tracer.cpp`) links against the real simulator sources, runs the program, and writes the machine state of every cycle to a JSON file. A standalone HTML page (`page.html`) loads that trace and replays it. Nothing in the simulator is re-implemented in the page, so the picture cannot disagree with what the simulator actually did.

```text
 program.asm
     |
     v
 +-----------+   steps the real PipelinedCore<4|6>     +--------------+
 |  simtrace |  (hardwired / hmicro / vmicro), one     | trace.json   |
 | tracer.cpp| --- cycle at a time, copying state ---> | (all cycles) |
 +-----------+                                         +------+-------+
                                                              |  embedded in
                                                              v
                                                    +------------------+
                                                    | trace.html       |
                                                    | (page.html + data|
                                                    |  standalone file)|
                                                    +------------------+
```

## 2. What it shows

For every cycle of every traced configuration the page can display:

| Area | Contents |
| :--- | :--- |
| **Pipeline** | One card per stage (IF, OF, EX, MA/RW for the 4-stage core; IF, OF, EX, MA1, MA2, RW for the 6-stage core) with the instruction, its address, operands, MAR/MDR where relevant, and the micro-word it is using. Bubbles, held stages (stalled) and fault markers are shown. Event chips below the stages explain *why*: forwarding, load-use stall, branch flush, retire, exception. |
| **Program** | Address, machine word, and disassembly of every instruction, with a tag showing which stage each instruction is currently in and a dot to set a breakpoint. |
| **Registers** | `r0`–`r13`, `sp`, `ra` in hex, signed integer or IEEE 754 float view, plus the flag LEDs (E, GT, LAND, LOR, LNOT) and the PC. Values that changed this cycle are highlighted. |
| **Data memory** | A stack view (grows down from `0x1000`, words below `sp` are not live) and a data view for the low addresses. A word written in this cycle is highlighted. |
| **Control unit** | The micro-words in flight (one per stage), the 36 control bits of the selected word, its 12-bit immediate, 10-bit branch target, 10-bit args and next micro-PC, and the ROM table with the in-flight words highlighted. In vertical mode it also shows the encoded datapath / pc / flag fields that a decoder expands into the same 36 lines. |
| **ALU** | For an instruction in EX: operation, operands **A** and **B** and the result in 32-bit binary, the upper word of a multiply, the flags (ERR, E, GT, LAND, LOR, LNOT), and the branch unit's decision (taken / target). |
| **FPU** | For a floating-point instruction in EX: operands and result split into sign / exponent / mantissa, their decoded values, the rounding mode, and the exception flags (invalid, divide-by-zero, overflow, underflow, inexact). |
| **Instruction × cycle chart** | A Gantt chart with one row per instruction and one column per cycle. Hatched cells are held by a stall, ✕ marks instructions flushed by a branch. Clicking a cell jumps to that cycle. |
| **Event log** | Every forward, stall, branch, retire and exception, in cycle order, filterable by type. |
| **Profiling** | Per-stage time up to the current cycle, with CPI, cycle, stall and flush counts for the whole run. |

## 3. How to read one cycle

Each cycle on screen is the state **after** the simulator executed that step, exactly what the debugger's `pipe` command prints. Three consequences follow from this and are worth knowing when presenting:

1. An instruction sitting in **EX** has its operands latched; its ALU result, flag update or branch decision becomes visible in the **next** cycle.
2. **Registers** change in the cycle after an instruction passes RW, and **memory** changes the cycle after it passes the memory stage.
3. **Retired** means an instruction has finished RW and left the pipeline, so its effect is final. Flushed instructions never retire. CPI is `cycles / retired instructions`, and a stall adds a cycle without adding a retired instruction, which is why CPI rises above 1.

### Example

`examples/array_sum.asm` on the 6-stage pipeline: at cycle 13 `add r8, r5, r6` is held in OF while `ld r6, 4[r1]` is still in MA2. The page shows a bubble in EX, the HELD tag on IF and OF, hatched cells in the chart, and the event chip *"load-use hazard: r6 is still being loaded by `ld r6, 4[r1]` (MA2); `add r8, r5, r6` waits in OF"*. Earlier, at cycles 7–9, the event log shows three forwards from `mov` instructions in RW into the `st` instructions that need their values, with no stall.

## 4. The trace file

`simtrace` writes one JSON document per program containing:

* **`program`** – name, source text and the list of words (address, hex, disassembly, labels).
* **`isa`**, **`machine`** – opcode and control-signal names; data-memory size, stack base/limit, initial `sp` and `ra`.
* **`micro`** – the horizontal ROM (93 words: 3 shared front-end words plus 3 per opcode), the vertical front-end words, the entry table (`3 + 3 × opcode`), and a check that the vertical encoding expands to the same words as the horizontal ROM.
* **`runs`** – one entry for each configuration: `pipe4-hard`, `pipe4-hmicro`, `pipe4-vmicro`, `pipe6-hard`, `pipe6-hmicro`, `pipe6-vmicro`. Each run holds the per-cycle snapshots, the instruction instances with their fate (retired / flushed / faulted), stall / flush / retire counters, per-stage latency, and a summary.
* **`selfCheck`** – result of the verification described in Section 6.

Every instruction is given a **dynamic id**, so the same static instruction fetched again after a flush, or held across several cycles by a stall, can be followed as one object. A **cycle snapshot** records the instruction in each stage (id, address, operands, results, MAR/MDR, fault, micro-word addresses), all registers and flags, any word-level memory write, the ALU / FPU / branch information for the instruction in EX, and the events of that cycle.

## 5. Building and running

The tracer lives in the project root, outside `src/`, because it has its own `main()` (otherwise `g++ src/*.cpp` would find two). The only change to the simulator sources is one line in `cpu.h` declaring the tracer a friend so it can read the pipeline latches:

```cpp
friend struct TraceExporter;   // visualizer trace exporter (tracer.cpp)
```

**Build** (from the RISC201 directory):

```bash
g++ -std=c++17 -O0 -g -Wl,--stack,268435456 -Isrc tracer.cpp $(ls src/*.cpp | grep -v '/main.cpp$') -o simtrace
```

**Run and open:**

```bash
./simtrace examples/array_sum.asm --html template.html
start trace.html            # xdg-open on Linux, open on macOS
```

| Option | Effect |
| :--- | :--- |
| `-o name.json` | Name of the trace file; with `--html` the page is written as `name.html`. |
| `--html template.html` | Embed the trace into the page template and write a standalone HTML file. |
| `--only pipe6-hmicro` | Trace one configuration (`pipe4` or `pipe6`, then `-hard`, `-hmicro` or `-vmicro`). |

`template.html` is `page.html` wrapped in a complete HTML document; the tracer replaces its `__TRACE_JSON__` marker with the trace. The generated `trace.html` is a single file that can be opened offline or sent to someone else. An existing `trace.json` can also be opened with the **Load trace** button or by dragging it onto the page.

## 6. Verification

* **Faithfulness.** Cycle counts and final register values from the tracer were compared with the unmodified simulator in all six pipelined configurations and were identical, so tracing does not change behaviour.
* **Self-check.** For the instruction in EX the tracer independently recomputes the ALU / FPU result and the branch decision and compares it with the value the pipeline actually latched. The demo program compares 72 results across the six runs with 0 mismatches. A mismatch count is printed after every run.
* **Vertical encoding.** The tracer confirms that decoding the vertical micro-words reproduces the horizontal control words.

## 7. Limitations

* The **single-cycle** baseline is not traced; the visualizer covers the six pipelined configurations.
* The self-check does not recompute `itof`, `ftoi` and `sqrt` results, so the ALU / FPU panel marks those opcodes as unsupported.
* Profiling values are wall-clock nanoseconds spent by the C++ simulator on the host machine. They show where the simulator spends its time and vary slightly between runs; they are not modelled hardware delays.
* The page loads two fonts from Google Fonts; without a network connection it falls back to system fonts and works the same otherwise.
