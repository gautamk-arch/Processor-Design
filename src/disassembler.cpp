#include "disassembler.h"
#include <sstream>
#include <iomanip>
#include <stdexcept>

static const string OPCODE_NAMES[]={
    "add", "sub", "mul", "div", "mod", "cmp", "and", "or",
    "not", "mov", "lsl", "lsr", "asr", "nop", "ld", "st",
    "beq", "bgt", "b", "call", "ret", "land","lor","lnot","bland","blor","blnot"
};
string formatInstruction(const Instruction& inst){
    int opIndex=static_cast<int> (inst.op);
    if(opIndex>26) return "Unkown Illegal Instruction";

    string mnemonic=OPCODE_NAMES[opIndex];

    if(inst.isImm){
        if(inst.mod==1)mnemonic+="u";
        else if(inst.mod==2) mnemonic+="h";
    }

    std::stringstream asmLine;
    asmLine<<mnemonic;

    switch(inst.op){
        case Opcode::nop:
        case Opcode::ret:
            break;
        case Opcode::beq:
        case Opcode::bgt:
        case Opcode::b:
        case Opcode::call:
        case Opcode::bland:
        case Opcode::blor:
        case Opcode::blnot:
            asmLine<<" "<<inst.imm;
            break;
        case Opcode::ld:
        case Opcode::st:
            asmLine<<" r"<<inst.rd<<", "<<inst.imm<<"[r"<<inst.rs1<<"]";
            break;
        case Opcode::cmp:
            asmLine<<" r"<<inst.rs1<<", ";
            if(inst.isImm) asmLine<<inst.imm;
            else asmLine <<"r"<<inst.rs2;
            break;
        case Opcode::not_op:
        case Opcode::mov:
            asmLine<<" r"<<inst.rd<<", ";
            if(inst.isImm) asmLine<<inst.imm;
            else asmLine <<"r"<<inst.rs2;
            break;
        case Opcode::land:
        case Opcode::lor:
        case Opcode::lnot:
            asmLine<<" r"<<inst.rs1<<", r"<<inst.rs2;
            break;
        default:
            asmLine<<" r"<<inst.rd<<", r"<<inst.rs1<<", ";
            if(inst.isImm) asmLine<<inst.imm;
            else asmLine <<"r"<<inst.rs2;
            break;
    }
    return asmLine.str();
}
string disassembleWord(const string& hexWord){
    uint32_t machineCode=std::stoul(hexWord,nullptr,16);
    Instruction inst=decode(machineCode);
    return formatInstruction(inst);
}
vector<string> disassembleProgram(const vector<string>& hexProgram){
    vector<string> assemblyLines;
    for(const auto& word:hexProgram){
        assemblyLines.push_back(disassembleWord(word));
    }
    return assemblyLines;
}