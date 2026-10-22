@ to check forwarding unit 

mov r1, 5
add r2, r1, 3      @ needs r1 from the previous instruction
add r3, r2, r1     @ needs r2 from the previous instruction
sub r4, r3, r2     @ needs r3 from the previous instruction