@ fpu_test.asm
@ Tests FPU math and IEEE 754 comparisons (cmpf)
@ Final success is indicated if r12 holds 1 (0x00000001)

@ 1. Load initial values
movf r1, 10.5
movf r2, 2.0

@ 2. Test Division (r3 = 10.5 / 2.0 = 5.25)
divf r3, r1, r2

@ 3. Test Multiplication (r5 = 5.25 * 4.0 = 21.0)
movf r4, 4.0
mulf r5, r3, r4

@ 4. Test Addition (r7 = 21.0 + 3.25 = 24.25)
movf r6, 3.25
addf r7, r5, r6

@ 5. Test Subtraction (r9 = 24.25 - 4.25 = 20.0)
movf r8, 4.25
subf r9, r7, r8

@ 6. Test Comparisons (cmpf)
movf r10, 20.0
cmpf r9, r10        @ Compare 20.0 with 20.0 (Should set E=1, GT=0)
beq .test_greater
b .fail             @ If not equal, FPU math or EQ flag failed

.test_greater:
movf r11, 25.0
cmpf r11, r9        @ Compare 25.0 > 20.0 (Should set E=0, GT=1)
bgt .success
b .fail             @ If not greater, GT flag failed

.success:
mov r12, 1          @ SUCCESS: Set r12 to 1 (integer mov)
b .end

.fail:
mov r12, 0          @ FAIL: Set r12 to 0 (integer mov)

.end: