#pragma once
#include <cstdint>
#include<iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <array>
using std::cout;
using std::vector;
using std::string;
enum class Opcode{
    add=0,sub=1,mul=2,div=3,mod=4,cmp=5,and_op=6,or_op=7,not_op=8,mov=9,lsl=10,lsr=11,asr=12,nop=13,ld=14,st=15,beq=16,bgt=17,b=18,call=19,ret=20,land=21,lor=22,lnot=23
};
struct Instruction{
    Opcode op;
    bool isImm;
    int mod;
    int rd,rs1,rs2;
    int32_t imm;
};
uint32_t encode(Instruction inst);
Instruction decode(uint32_t word);
uint32_t ImmValue(int mod,uint16_t imm16);