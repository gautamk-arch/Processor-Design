    mov r1, 42

overflow_loop:
    push r1
    cmp r1, r1
    beq overflow_loop
@ this makes an infinite loop because the condition is always true, so there will be stack overflow when it reaches the limit