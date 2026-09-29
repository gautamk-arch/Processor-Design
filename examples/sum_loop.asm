    mov r1, 0       @ r1 will contain the sum
    mov r2, 10

loop:
    add r1, r1, r2
    sub r2, r2, 1
    cmp r2, 0
    bgt loop