@ to check if there are back to back dependent instructions like read after write
    mov r1, 10
    add r2, r1, 5
    sub r3, r2, 10
    mul r4, r3, r1
@ here when we are writing these commands the dest reg is immediatly dependent on operand registers and writes the value just after that