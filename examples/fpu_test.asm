@ fpu_test.asm
@ Tests FPU math and IEEE 754 comparisons (cmpf)
@ Final success is indicated if r12 holds 1 (0x00000001)

@ 1. Load initial values
@10.5 in float format
movf r1, 1093140480 
@2.0 in float format
movf r2,1073741824

@ 2. Test Division (r3 = 10.5 / 2.0 = 5.25)
divf r3, r1, r2

@ 3. Test Multiplication (r5 = 5.25 * 4.0 = 21.0)
@4.0 in float format
movf r4, 1082130432
mulf r5, r3, r4

@ 4. Test Addition (r7 = 21.0 + 3.25 = 24.25)
@3.25 in float format
movf r6, 1078984704
addf r7, r5, r6

@ 5. Test Subtraction (r9 = 24.25 - 4.25 = 20.0)
@4.25 in float format
movf r8, 1082654720
subf r9, r7, r8

@ 6. Test Comparisons (cmpf)
@20.0 in float format
movf r10, 1101004800
cmpf r9, r10        @ Compare 20.0 with 20.0 (Should set E=1, GT=0)
beq .test_greater
b .fail             @ If not equal, FPU math or EQ flag failed

.test_greater:
@25.0 in float format
movf r11, 1103626240
cmpf r11, r9        @ Compare 25.0 > 20.0 (Should set E=0, GT=1)
bgt .success
b .fail             @ If not greater, GT flag failed

.success:
mov r12, 1          @ SUCCESS: Set r12 to 1 (integer mov)
b .end

.fail:
mov r12, 0          @ FAIL: Set r12 to 0 (integer mov)

.end: