@ ==============================================================================
@ RISC201 COMPLETE ISA & RUNTIME EXCEPTION LIMIT TEST
@ Covers all 30 Opcodes (0..29), Modifiers (u, h), Macros (PUSH, POP), and FPU
@ ==============================================================================

@ --- 1. Stack Initialization ---
movu sp, 4096            @ Initialize Stack Pointer (r14) to top of data memory

@ ==============================================================================
@ RUNTIME EXCEPTION TRIGGERS (Uncomment ONE line below to test each exception)
@ ==============================================================================
@ b trap_stack_overflow
@ b trap_stack_underflow
@ b trap_div_zero
@ b trap_misaligned_mem
@ b trap_bad_address
@ b trap_fpu_nan

b main_test              @ Opcode 18 (b): Unconditional jump to main test suite

@ ------------------------------------------------------------------------------
@ EXCEPTION TRAP ROUTINES (Stage 3 & FPU Runtime Protection)
@ ------------------------------------------------------------------------------
trap_stack_overflow:
    sub sp, sp, 4        @ Decrement SP continuously in an infinite loop
    st r1, 0[sp]         @ Triggers StackOverflow once SP crosses STACK_LIMIT
    b trap_stack_overflow

trap_stack_underflow:
    add sp, sp, 64       @ Push SP above STACK_BASE (4096)
    ld r1, 0[sp]         @ Triggers StackUnderflow on stack read
    ret

trap_div_zero:
    mov r1, 100
    mov r2, 0
    div r3, r1, r2       @ Triggers DivideByZero in ALU
    ret

trap_misaligned_mem:
    mov r1, 10
    st r1, 2[r0]         @ Address 2 is not 4-byte aligned -> Triggers Misaligned
    ret

trap_bad_address:
    movh r1, 1           @ r1 = 0x00010000 (65536, well outside 4096B memory)
    ld r2, 0[r1]         @ Triggers BadAddress
    ret

trap_fpu_nan:
    mov r1, -16
    itof r2, r1          @ Convert -16 to IEEE 754 float (-16.0f)
    sqrt r3, r2          @ Square root of negative float -> Triggers FPU Invalid (NaN)
    ret

@ ==============================================================================
@ MAIN INSTRUCTION TEST SUITE (Tests Opcodes 0 to 29)
@ ==============================================================================
main_test:
    @ --- Opcodes 9 & 13: mov (default, u, h) & nop ---
    nop                  @ Opcode 13: No operation
    mov r1, 25           @ Opcode 9: Default sign-extended immediate (mod = 0)
    mov r2, -5           @ Opcode 9: Negative sign-extended immediate
    movu r3, 65535       @ Opcode 9: Zero-extended immediate (mod = 1, 0x0000FFFF)
    movh r4, 1           @ Opcode 9: High-shifted immediate (mod = 2, 0x00010000)

    @ --- Opcodes 0, 1, 2, 3, 4: Arithmetic (add, sub, mul, div, mod) ---
    add r5, r1, r2       @ Opcode 0 (Reg): 25 + (-5) = 20
    add r5, r5, 10       @ Opcode 0 (Imm): 20 + 10 = 30
    sub r6, r5, r1       @ Opcode 1 (Reg): 30 - 25 = 5
    mul r7, r5, r6       @ Opcode 2 (Reg): 30 * 5 = 150
    div r8, r7, 10       @ Opcode 3 (Imm): 150 / 10 = 15
    mod r9, r8, 4        @ Opcode 4 (Imm): 15 % 4 = 3

    @ --- ALU Edge Case: INT_MIN / -1 Guard ---
    movh r10, 32768      @ 0x80000000 (INT_MIN)
    div r10, r10, -1     @ Tests C++ UB guard in ALU for INT_MIN / -1

    @ --- Opcodes 6, 7, 8: Bitwise Logicals (and, or, not) ---
    and r10, r8, r9      @ Opcode 6: 15 & 3 = 3
    or  r11, r8, 16      @ Opcode 7: 15 | 16 = 31
    not r12, r10         @ Opcode 8: ~3 = -4

    @ --- Opcodes 10, 11, 12: Shifts (lsl, lsr, asr) & Edge Shifts (0, 31) ---
    lsl r10, r9, 2       @ Opcode 10: 3 << 2 = 12
    lsr r10, r10, 1      @ Opcode 11: 12 >> 1 = 6
    asr r12, r12, 1      @ Opcode 12: -4 >> 1 = -2 (preserves sign bit)
    lsl r10, r10, 0      @ Shift by 0 edge case
    lsr r11, r3, 31      @ Shift by 31 edge case

    @ --- Opcodes 14 & 15: Memory Access (ld, st) & Preprocessor PUSH/POP ---
    st r7, 0[r0]         @ Opcode 15: Store 150 at Data Memory[0]
    ld r13, 0[r0]        @ Opcode 14: Load 150 into r13 from Data Memory[0]
    PUSH r13             @ Preprocessor Macro: sub sp, sp, 4 + st r13, 0[sp]
    POP r1               @ Preprocessor Macro: ld r1, 0[sp] + add sp, sp, 4

    @ --- Opcodes 27, 28, 29: Floating Point Unit (itof, sqrt, ftoi) ---
    mov r2, 144
    itof r3, r2          @ Opcode 27: Convert integer 144 -> float 144.0f
    sqrt r4, r3          @ Opcode 29: sqrt(144.0f) -> 12.0f
    ftoi r5, r4          @ Opcode 28: Convert float 12.0f -> integer 12

    @ --- Opcodes 21, 22, 23, 24, 25, 26: Custom Logical Flags & Branches ---
    mov r6, 1
    mov r7, 0
    land r5, r6          @ Opcode 21: (12 && 1) -> sets flag_land = 1
    bland pass_land      @ Opcode 24: Branch taken because flag_land == 1
    mov r1, 999          @ Should be skipped

pass_land:
    lor r5, r7           @ Opcode 22: (12 || 0) -> sets flag_lor = 1
    blor pass_lor        @ Opcode 25: Branch taken because flag_lor == 1
    mov r1, 999          @ Should be skipped

pass_lor:
    lnot r7, r0          @ Opcode 23: (!0) -> sets flag_lnot = 1
    blnot pass_lnot      @ Opcode 26: Branch taken because flag_lnot == 1
    mov r1, 999          @ Should be skipped

pass_lnot:
    @ --- Opcodes 5, 16, 17, 19, 20: cmp, beq, bgt, call, ret ---
    mov r1, 10
    mov r2, 20
    call verify_branches @ Opcode 19: Saves return address in ra (r15) and jumps
    b finish_program     @ Jump past subroutine to let PC finish naturally

@ ------------------------------------------------------------------------------
@ SUBROUTINE: Tests Function Prologue/Epilogue, cmp, bgt, beq, and ret
@ ------------------------------------------------------------------------------
verify_branches:
    sub sp, sp, 8        @ Allocate 2 words on stack
    st ra, 4[sp]         @ Backup return address (ra / r15)
    st r1, 0[sp]         @ Backup caller r1

    cmp r2, r1           @ Opcode 5: 20 vs 10 -> sets GT = 1, E = 0
    bgt is_greater       @ Opcode 17: Taken since 20 > 10
    b exit_sub

is_greater:
    cmp r1, 10           @ Opcode 5 (Imm): 10 vs 10 -> sets E = 1, GT = 0
    beq is_equal         @ Opcode 16: Taken since 10 == 10
    b exit_sub

is_equal:
    add r1, r1, r2       @ r1 = 30

exit_sub:
    ld ra, 4[sp]         @ Restore ra
    add sp, sp, 8        @ Deallocate stack frame
    ret                  @ Opcode 20: Jump back to ra (r15)

finish_program:
    nop                  @ Final instruction@ PC advances past end of program to stop