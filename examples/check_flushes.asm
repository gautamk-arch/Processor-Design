@ program to check flushes when branch taken and not taken. also later to check branch predictor

    mov r1, 0
    mov r2, 3
.loop:  
    add r1, r1, 1
    cmp r1, r2     @ uses r1 from the previous instruction
    beq .done
    b   .loop
.done:  
    mov r3, r1
    mov r4, 99