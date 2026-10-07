@ adds the value in r1, r2, r3, r4
@ expected result = 60(for this case) stored in r8
    mov r1, 0
    mov r2, 10
    mov r3, 20
    mov r4, 30

    st r2, 0[r1]
    st r3, 4[r1]
    st r4, 8[r1]

    ld r5, 0[r1]
    ld r6, 4[r1]
    ld r7, 8[r1]

    add r8, r5, r6
    add r8, r8, r7