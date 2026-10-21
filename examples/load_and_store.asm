@ program to check the load stall when it immediately follows a store

mov r1, 16
mov r2, 7
st  r2, 0[r1]      @ store data r2 comes from the previous instruction
ld  r3, 0[r1]
add r4, r3, 1      @ uses the load result immediately: stall
add r5, r4, r3     @ r4 forwarded, r3 from the register file