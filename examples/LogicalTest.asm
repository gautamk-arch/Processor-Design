mov r1,5
mov r2,2
mov r3,3
mov r4,4
cmp r1,r3
bgt foo1
mov r5,0
b foo12
foo1:
mov r5,1
foo12:

cmp r2,r4
bgt foo2
mov r6,0
b foo21
foo2:
mov r6,1
foo21:
land r5,r6
bland foo3
mov r7,0
b foo31
foo3:
mov r7,1
foo31:

@This is assembly code for making-
@if((5>3) && (2>4)) in an efficient manner