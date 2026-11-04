@ This program acts as a Vector Magnitude Calculator. It iterates through an array of 2D points (X, Y) in memory, calculates the hypotenuse H = sqrt{X^2 + Y^2} FPU instructions, converts the result back to an integer, and stores it in a new array.


@ --- INITIALIZATION ---
mov r1, 0           @ r1 = loop counter (i = 0)
mov r2, 4           @ r2 = loop limit (process 4 vectors)
mov r3, 100         @ r3 = Base address for Input Array (X, Y pairs)
mov r4, 200         @ r4 = Base address for Output Array (Results)

loop_start:
@ --- CONTROL HAZARD TEST ---
cmp r1, r2          @ Compare i with limit. Sets E flag if equal.
beq end_loop        @ Branch to end if E flag is set (i == 4).

@ --- LOAD-USE STALL & FORWARDING TEST ---
ld r5, 0[r3]      @ r5 = Load X from Memory[r3 + 0]
itof r7, r5         @ r7 = float(X) -> RAW Hazard! (Triggers load-use stall)

ld r6, 4[r3]        @ r6 = Load Y from Memory[r3 + 4]
itof r8, r6        @ r8 = float(Y) -> RAW Hazard! (Triggers load-use stall)

@ --- FPU MATH & FORWARDING TEST ---
mulf r7, r7, r7     @ r7 = X^2 (Forwarded from itof EX stage)
mulf r8, r8, r8     @ r8 = Y^2 (Forwarded from itof EX stage)
addf r9, r7, r8     @ r9 = X^2 + Y^2
sqrt r10, r9       @ r10 = sqrt(X^2 + Y^2) (Unary FPU test)
ftoi r11, r10       @ r11 = int(hypotenuse) (Unary FPU test)

@ --- MEMORY WRITE TEST ---
st r11, 0[r4]       @ Store result into Memory[r4 + 0]

@ --- POINTER ARITHMETIC ---
add r3, r3, 8       @ Advance input pointer by 8 bytes (two 32-bit words)
add r4, r4, 4       @ Advance output pointer by 4 bytes (one 32-bit word)
add r1, r1, 1       @ Increment loop counter (i++)

@ --- BRANCH FLUSH TEST ---
b loop_start        @ Unconditional jump back to loop_start

end_loop:
nop                 @ Pipeline drain
nop
nop