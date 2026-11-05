# Arithmetic Logical Unit (ALU)

## 1. Purpose
The ALU is the 32-bit integer execution unit. For each operation, it returns a result value, an optional upper word (multiply only), and status flags (equal, greater-than, logical flags, error). Every addition and subtraction, including those inside multiply and divide, goes through one Brent-Kung adder.

![ALU Architecture Diagram](aludiag1.png)

## 2. Brent-Kung Adder
*   **Step 1:** Each bit pair computes generate (`a AND b`, so this bit creates a carry) and propagate (`a XOR b`, so this bit passes an incoming carry on).
*   **Up-sweep:** A tree merges neighboring groups with `G = G_hi OR (P_hi AND G_lo)` and `P = P_hi AND P_lo`. The group size doubles each level (1, 2, 4, 8, 16), so after `log2(n)` levels the positions 1, 3, 7, 15, and 31 know the carry out of everything below them.
*   **Down-sweep:** A reverse tree of `log2(n) - 1` levels fills in the carries for the remaining in-between bit positions by reusing the groups already built.
*   **Sum:** The external carry-in is merged in (`carry = G OR (P AND cin)`). Each sum bit is then the bit's propagate XOR the carry arriving from the bit below.
*   **Trade-off:** Brent-Kung needs about `2n` combine cells and `2 * log2(n) - 1` levels (9 for 32 bits). A dense tree such as Kogge-Stone needs about `n * log2(n)` cells but only `log2(n)` levels. Brent-Kung therefore has less hardware and wiring, at the cost of a little more delay. The same adder is used at 32-bit and 33-bit widths.
*   **Width helper:** The adder is parameterized by bit width, and the mask helper keeps results inside it. The extra 33rd bit holds the sign or borrow in Booth and division.

![Brent-Kung Adder Flowchart](aludiag2.png)

## 3. Arithmetic Operations
*   **ADD/SUB / negate:** ADD is the adder. SUB is `A + (~B) + 1`, with the `+1` supplied as carry-in. Negation is `~x + 1`, used for sign handling in division.
*   **MUL (Booth):** Radix-2 Booth multiplication on a 33-bit accumulator and the multiplier. For each of 32 steps, it looks at (current bit, previous bit). A `10` pair subtracts the multiplicand, a `01` pair adds it, and `00` or `11` does nothing. The accumulator and multiplier are then shifted right arithmetically as one register. The result is a full 64-bit product (low word in `val`, high word in `hi`).
*   **DIV/MOD (non-restoring):** The operands are first converted to magnitudes. Over 32 steps the remainder/dividend pair shifts left, then the divisor is added if the partial remainder is negative and subtracted if not. The quotient bit is 1 when the remainder is non-negative afterwards. A final correction add fixes a negative remainder. There is one add or subtract per bit and no restore step.
*   **Division signs:** The quotient is negative when the operand signs differ, and the remainder takes the dividend's sign. Dividing by zero raises the error flag.

## 4. Shifts and Logic
*   **Shifts (LSL, LSR, ASR):** A barrel shifter with 5 stages (shift by 1, 2, 4, 8, 16), each enabled by one bit of the shift amount, so any shift of 0 to 31 takes a fixed 5 steps. Amounts above 31 give 0, or sign fill for ASR. ASR fills with the sign bit.
*   **Bitwise ops and MOV:** AND, OR and NOT work bit by bit on all 32 bits and return a value. MOV passes operand B through.

## 5. Custom Logical Instructions
*   **Three new instructions (logical AND / OR / NOT):** `land`, `lor` and `lnot` treat each operand as a boolean (non-zero means true). They produce a single true/false flag instead of a register value. 
    *   `land` is true if both operands are non-zero.
    *   `lor` is true if either is non-zero.
    *   `lnot` is true if the operand is zero. 
*   They are separate from the bitwise AND/OR/NOT, and the new branches `bland`, `blor` and `blnot` act on their flags.

## 6. Comparison and Overflow Handling
*   **CMP:** Computes `A - B` with the adder. It sets equal when the difference is zero. It sets greater-than when the values are not equal and not less-than, where less-than is the sign of the difference XOR signed overflow. These flags drive `beq` and `bgt`.
*   **Overflow in ADD / SUB:** Results wrap modulo 2^32 (two's complement), as in C. The adder's carry-out is discarded and no overflow flag is raised.
*   **Overflow in comparison:** CMP detects signed overflow explicitly (operands of opposite sign and a result whose sign differs from the first operand). Comparisons stay correct even when `A - B` overflows, for example INT_MAX vs -1.
*   **Overflow in MUL:** It cannot occur. The accumulator is sign-extended to 33 bits and the full 64-bit product is returned. Software can detect an overflowing 32-bit result by checking whether `hi` is just the sign extension of `val`.
*   **Overflow in DIV / MOD:** The divider works on magnitudes in a 33-bit remainder register, so `|INT_MIN| = 2^31` fits. Division by zero is flagged as an error. The one wrapping case is `INT_MIN / -1`, which returns `INT_MIN`.