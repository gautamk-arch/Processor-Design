# Pipelining & Hazard Resolution

## 1. Architectural Overview

### 4-Stage Pipeline
The 4-stage pipeline architecture (`PipelinedCore<4>`) streamlines execution into four distinct phases:
*   **IF (Instruction Fetch):** Reads the current PC and fetches the corresponding 32-bit instruction from memory.
*   **OF (Operand Fetch):** Decodes the instruction, reads required values from the register file, sets up the `storeVal` register for store operations, and performs the hazard scan to determine if a stall is necessary.
*   **EX (Execute):** Executes all arithmetic (ALU) and floating-point (FPU) operations. It evaluates condition flags (such as `flag_E`, `flag_GT`, `land`, `lor`, `lnot`) and calculates targets to resolve branches.
*   **MA/RW (Memory Access & Register Write-Back):** This is a combined stage. It handles reading from or writing to the simulated data memory, and immediately writes the final computed result (or loaded memory value) back into the destination register.
*   **Design Decision:** The MA and RW stages are combined to shorten the overall pipeline length. This reduces the latency penalty when an instruction must wait for a previous memory load (shrinking the load-use hazard window) and simplifies the control logic.

![4-Stage Datapath Diagram](pipeline1.png)

### 6-Stage Pipeline
The 6-stage pipeline architecture (`PipelinedCore<6>`) extends the memory phase to optimize performance and handle exceptions safely:
*   **IF, OF, EX:** These stages function identically to their 4-stage counterparts.
*   **MA1 (Memory Access 1 - Address Generation):** Computes the effective memory address and stores it in the Memory Address Register (MAR). Crucially, this stage performs all memory bounds checking and stack safety validation. If a fault (like StackOverflow or an out-of-bounds access) is detected, it is captured as a `std::exception_ptr` but *not* immediately thrown.
*   **MA2 (Memory Access 2 - Physical Access):** Reads from or writes to the physical RAM via the Memory Data Register (MDR). If MA1 captured a fault, MA2 is the stage that officially throws the exception.
*   **RW (Register Write-Back):** Writes the final result from the ALU, FPU, or MDR back into the designated architectural register.
*   **Design Decision:** Splitting memory access across MA1 and MA2 allows the hardware to calculate the address and validate safety constraints before attempting a physical read/write. Deferring the exception to MA2 ensures precise exception handling—older instructions are allowed to finish writing back their results before the pipeline crashes, perfectly synchronizing the hardware state.

![6-Stage Datapath Diagram](pipeline2.png)

## 2. Implementation Mechanics

### The Reverse Evaluation Engine
The CPU simulator uses a Reverse Evaluation Engine within its main loop to emulate parallel hardware latches using sequential C++ code.
*   The loop iterates backward (e.g., from Stage 5 down to Stage 0).
*   By processing the oldest instruction in the pipeline first (draining MA/RW), it frees up the latch for the next instruction (in EX) to move forward.
*   This prevents a younger instruction from overwriting data that is still in flight, ensuring a cycle-accurate simulation of parallel data movement.

### The Slot Packet Structure
Pipeline stages communicate by passing a lightweight `Slot` struct rather than heavy, fully decoded control words.
*   The `Slot` carries only essential data: the raw instruction, the PC, the extracted operands (`a`, `b`, `storeVal`), the computed `result`, and a `valid` boolean bit.
*   If `valid` is set to false (a "ghost" or bubble), the stages simply ignore the slot, preventing corrupted register writes or invalid memory accesses without requiring complex rollback code.

## 3. Hazard Detection & Resolution Logic

### Data Hazards & Forwarding
The processor resolves structural data dependencies dynamically using a producer-consumer forwarding network.
*   During the **OF** stage, the hazard unit scans downstream stages (EX, MA1, MA2, RW) to locate the youngest in-flight instruction that writes to a register the current instruction needs.
*   If a match is found, the OF stage intercepts the computed ALU/FPU result or memory data directly from that downstream stage and forwards it into the current execution slot, bypassing the need to wait for the register file to update.

### Load-Use Stalls
When an instruction requires data that is currently being loaded from memory, forwarding alone cannot resolve the dependency because the data is not yet available in the EX stage.
*   **4-Stage Penalty:** The pipeline inserts a **1-cycle stall** (delaying the OF stage) until the load instruction completes its combined MA/RW stage.
*   **6-Stage Penalty:** The pipeline inserts a **2-cycle stall** (delaying the OF stage) until the load instruction fully completes its MA2 physical access cycle.
*   The stall logic mathematically halts the OF stage, forcing the IF stage to hold its current PC while inserting invalid bubbles into the EX stage.

### Control Hazards & Flushes
Control hazards occur when a branch or jump dictates a change in execution flow.
*   **Resolution in EX:** All branch targets and conditions are evaluated in the EX stage (two stages after the instruction was fetched).
*   **The Flush Mechanism:** If a branch is *taken*, the EX stage overwrites the global PC with the new target address.
*   It then iterates backward through the IF and OF stages (which currently hold instructions from the wrong execution path) and sets their `valid` flags to `false`.
*   These "ghost" instructions flow harmlessly through the rest of the pipeline. The cost of a taken branch is exactly 2 wasted fetch cycles.

## 4. Microprogramming Integration
The pipelined processor utilizes an advanced, dynamic control mechanism: **On-Demand Fetching**.
*   Instead of decoding a massive control word in the OF stage and passing it down through every pipeline latch, each hardware block (EX, MA, RW) independently queries the Control ROM.
*   Based on the instruction's opcode and the current stage step (0 for EX, 1 for MA, 2 for RW), the block fetches its exact control signals precisely on the clock cycle they are required.
*   **Architectural Benefits:** This design significantly reduces the physical wiring footprint of the pipeline latches. Furthermore, when a branch flush occurs, there are no stale control signals propagating through the pipeline to clean up—invalidating the `Slot` automatically stops the downstream stages from fetching control words.