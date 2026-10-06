mov r0, 10
mov r1, 1
call .factorial
b .end

.factorial:
    cmp r0, 1
    beq .foo1
    bgt .continue
    cmp r0, 0
    beq .foo1
    b .returnE

.continue:
    push ra
    push r0
    sub r0, r0, 1
    call .factorial
    pop r0
    pop ra
    mul r1, r0, r1
    ret

.foo1:
    mov r1, 1
    ret

.returnE:
    mov r1, -1
    ret

.end: