#include "isa.h"
uint32_t ImmValue(int mod,uint16_t imm16){
    if(mod==0){
        int16_t signed_imm= static_cast<int16_t>(imm16);
        return static_cast<uint32_t>(static_cast<int32_t>(signed_imm));
    }
    else if(mod==1){
        return static_cast<uint32_t>(imm16);
    }
    else if(mod==2){
        return (static_cast<uint32_t>(imm16)<<16);
    }
    else{
        throw std::invalid_argument("Illegal immediate modifier");
    }
}

uint32_t encode(Instruction inst){
    uint32_t word=0;
    word=word|( (static_cast<uint32_t>(inst.op)&0x1F) <<27 );
    
    bool isBranch= (inst.op==Opcode::nop || inst.op==Opcode::ret ||inst.op==Opcode::call ||inst.op==Opcode::b ||inst.op==Opcode::beq ||inst.op==Opcode::bgt||inst.op==Opcode::bland || inst.op==Opcode::blor || inst.op==Opcode::blnot);
    
    if(isBranch){
        uint32_t offset=static_cast<uint32_t>(inst.imm) & 0x7FFFFFF;
        word=word|offset;
    }
    else{
        word|=(static_cast<uint32_t>(inst.rd) & 0xF)<<22;
        word|=(static_cast<uint32_t>(inst.rs1) & 0xF)<<18;

        if(inst.isImm){
            word|=(1<<26);
            word|=(static_cast<uint32_t>(inst.mod) & 0x3)<<16;
            word|=(static_cast<uint32_t>(inst.imm) & 0xFFFF); 
        }
        else{
            word|=(static_cast<uint32_t>(inst.rs2) & 0xF)<<14;
        }
    }
    return word;
}

Instruction decode(uint32_t word){
    Instruction inst;
    inst.op=static_cast<Opcode>((word>>27)& 0x1F);

    bool isBranch= (inst.op==Opcode::nop|| inst.op==Opcode::ret ||inst.op==Opcode::call ||inst.op==Opcode::b ||inst.op==Opcode::beq ||inst.op==Opcode::bgt||inst.op==Opcode::bland || inst.op==Opcode::blor || inst.op==Opcode::blnot);

    if(isBranch){
        inst.isImm=false;
        inst.mod=0;
        inst.rd=0;
        inst.rs1=0;
        inst.rs2=0;

        uint32_t offset=word & 0x7FFFFFF;
        if(offset & 0x4000000){
            offset|=0xF8000000;
        }
        inst.imm=static_cast<int32_t>(offset);
    }
    else{
        inst.isImm=((word>>26)&0x1)==1;
        inst.rd=(word>>22)& 0xF;
        inst.rs1=(word>>18)& 0xF;
        if(inst.isImm){
            inst.mod=(word>>16)&0x3;
            inst.imm=static_cast<int32_t>(word & 0xFFFF);
            inst.rs2=0;
        }
        else{
            inst.rs2=(word>>14)&0xF;
            inst.mod=0;
            inst.imm=0;
        }
    }
    return inst;
}
