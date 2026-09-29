#pragma once
#include "assembler.h"
#include "isa.h"
#include <unordered_map>
#include <algorithm>
#include <sstream>

static string removeCommentsAndSpace(std::string& line,bool& inBlockofComments){
    if(inBlockofComments){
        size_t end= line.find("*/");
        if(end!=std::string::npos){
            line.erase(0,end+2);
            inBlockofComments=false;
        }
        else{
            return "";
        }
    }

    size_t start=line.find("/*");
    while(start!=string::npos){
        size_t end=line.find("*/",start+2);
        if(end!=std::string::npos){
            line.erase(start,end-start+2);
        }
        else{
            line.erase(start);
            inBlockofComments=true;
            break;
        }
        start=line.find("/*");
    }
    size_t atPos=line.find('@');
    if(atPos!=string::npos){
        line.erase(atPos);
    }

    if(line.empty()) return "";
    size_t first=line.find_first_not_of(" \t\r\n");
    if(first==string::npos) return "";
    size_t last=line.find_last_not_of(" \t\r\n");
    return line.substr(first,(last-first+1));
}

static int32_t CheckImmediate(const string& immStr,int mod,int lineNum,vector<string>& errors){
    try{
        long long rawImm=std::stoll(immStr,nullptr,0);
        if(mod==0){
            if(rawImm<-32768||rawImm>32767){
                errors.push_back("Line " + std::to_string(lineNum) + ": Imm out of bounds mod=0");
            }
        }
        else if(mod==1){
            if (rawImm<0||rawImm >65535) {
                errors.push_back("Line " + std::to_string(lineNum) + ": Imm out of bounds mod=1");
            }
        }
        else if(mod==2){
            if (rawImm <-32768||rawImm>65535) {
                errors.push_back("Line " + std::to_string(lineNum) + ": Imm out of bounds mod=2");
            }
        }
        return static_cast<int32_t>(rawImm);
    }
    catch(...){
        errors.push_back("Line " + std::to_string(lineNum) + ": Illegal imm value or format'" + immStr + "'");
        return 0;
    }
}
static int parseRegister(const std::string& regStr,int lineNum,vector<string>& errors){
    if(regStr== "sp") return 14;
    if(regStr=="ra") return 15;
    if(regStr.size()>1 && regStr[0]=='r'){
        try{
            int reg=std::stoi(regStr.substr(1));
            if(reg>=0 && reg<=15 ) return reg;
        }
        catch(...){}
    }
    errors.push_back("Line "+std::to_string(lineNum)+ ":Illegal Register number"+regStr+"'");
    return 0;
}

vector<uint32_t> assemble(const string& src,vector<string>& errors){
    vector<uint32_t> machineCode;
    std::unordered_map<string,uint32_t> symbolTable;
    std::vector<std::pair<std::string,int>> cleanedLines;

    std::istringstream stream(src);
    string line;
    int lineNum=0;
    bool inBlockofComments=false;

    uint32_t currAddr=0;
    while(std::getline(stream,line)){
        lineNum++;
        string stripped=removeCommentsAndSpace(line,inBlockofComments);
        if(stripped.empty()) continue;

        //finding labels
        size_t colonPos= stripped.find(':');
        if(colonPos!=string::npos){
            string label=stripped.substr(0,colonPos);
            label.erase(label.find_last_not_of(" /t")+1);
            symbolTable[label]=currAddr;
            stripped=stripped.substr(colonPos+1);
            stripped.erase(0,stripped.find_first_not_of(" /t"));
        }
        if(!stripped.empty()){
            cleanedLines.push_back({stripped,lineNum});
            currAddr+=4;

        }
    }
    //pass 2
    currAddr=0;
    for(const auto& [instStr,lNum]:cleanedLines){
        Instruction inst{};
        inst.mod=0;
        inst.isImm=false;
        std::istringstream iss(instStr);
        string mnemonic;
        iss>>mnemonic;

        if(!mnemonic.empty()){
            char lastchar=mnemonic.back();
            if(lastchar=='u' || lastchar=='h'){
                inst.mod = (lastchar=='u')? 1:2;
                mnemonic.pop_back();
                if(!mnemonic.empty() && mnemonic.back()=='.'){
                    mnemonic.pop_back();
                }
            }
        }

        string operandStr;
        std::getline(iss,operandStr);
        if(!operandStr.empty()){
            operandStr.erase(0,operandStr.find_first_not_of(" /t"));
            operandStr.erase(std::remove_if(operandStr.begin(),operandStr.end(),::isspace),operandStr.end());
        }

        vector<string> ops;
        std::stringstream opStream(operandStr);
        string op;
        while(std::getline(opStream,op,',')){
            ops.push_back(op);
        }

        //mapping the opcode
        if (mnemonic == "add") inst.op = static_cast<Opcode>(0); 
        else if (mnemonic == "sub") inst.op = static_cast<Opcode>(1);
        else if (mnemonic == "mul") inst.op = static_cast<Opcode>(2); 
        else if (mnemonic == "div") inst.op = static_cast<Opcode>(3);
        else if (mnemonic == "mod") inst.op = static_cast<Opcode>(4); 
        else if (mnemonic == "cmp") inst.op = static_cast<Opcode>(5);
        else if (mnemonic == "and") inst.op = static_cast<Opcode>(6); 
        else if (mnemonic == "or") inst.op = static_cast<Opcode>(7);
        else if (mnemonic == "not") inst.op = static_cast<Opcode>(8); 
        else if (mnemonic == "mov") inst.op = static_cast<Opcode>(9);
        else if (mnemonic == "lsl") inst.op = static_cast<Opcode>(10); 
        else if (mnemonic == "lsr") inst.op = static_cast<Opcode>(11);
        else if (mnemonic == "asr") inst.op = static_cast<Opcode>(12); 
        else if (mnemonic == "nop") inst.op = static_cast<Opcode>(13);
        else if (mnemonic == "ld") inst.op = static_cast<Opcode>(14); 
        else if (mnemonic == "st") inst.op = static_cast<Opcode>(15);
        else if (mnemonic == "beq") inst.op = static_cast<Opcode>(16); 
        else if (mnemonic == "bgt") inst.op = static_cast<Opcode>(17);
        else if (mnemonic == "b") inst.op = static_cast<Opcode>(18); 
        else if (mnemonic == "call") inst.op = static_cast<Opcode>(19);
        else if (mnemonic == "ret") inst.op = static_cast<Opcode>(20);
        else{
            errors.push_back("Line"+std::to_string(lNum)+":Unknown Operation"+mnemonic+"'");
            currAddr+=4;
            continue;
        }
        int currOp=static_cast<int> (inst.op);

        if(currOp<=13){
            if(currOp!=13){
                int nextOpIndex = 0;
                
                if (currOp == 5) {
                    inst.rd = 0; 
                    inst.rs1 = parseRegister(ops[nextOpIndex++], lNum, errors);
                } else {
                    inst.rd = parseRegister(ops[nextOpIndex++], lNum, errors);
                    if(currOp!=8 && currOp!=9){
                        inst.rs1 = parseRegister(ops[nextOpIndex++], lNum, errors);
                    }
                }

                if(nextOpIndex<ops.size()){
                    if(ops[nextOpIndex].find('r')!=string::npos || ops[nextOpIndex]=="sp"|| ops[nextOpIndex]=="ra"){
                        inst.rs2=parseRegister(ops[nextOpIndex],lNum,errors);
                        inst.isImm=false;
                        if(inst.mod!=0){
                            errors.push_back("Line "+std::to_string(lNum)+": u/h modifiers used with a register operand");
                        }
                    }
                    else {
                        inst.isImm=true;
                        inst.imm=CheckImmediate(ops[nextOpIndex], inst.mod, lNum, errors);
                    }
                }
            }
        }
        else if(currOp==14||currOp==15){
            inst.rd=parseRegister(ops[0],lNum,errors);
            inst.isImm=true;
            size_t bracketStart=ops[1].find('[');
            size_t bracketEnd=ops[1].find(']');
            
            if(bracketStart!=string::npos && bracketEnd!=string::npos){
                string immStr=ops[1].substr(0,bracketStart);
                if(inst.mod!=0){
                    errors.push_back("Line " + std::to_string(lNum) + ": ld/st having modifiers");
                }
                inst.imm=immStr.empty()? 0:std::stoi(immStr,nullptr,0);

                string rs1Str=ops[1].substr(bracketStart+1,bracketEnd-bracketStart-1);
                inst.rs1=parseRegister(rs1Str,lNum,errors);
            }
            else{
                errors.push_back("Line "+std::to_string(lNum)+": illegal ld/st format");
            }
        }

        else if(currOp>=16 && currOp<=19){
            string target =ops[0];
            if(symbolTable.count(target)){
                inst.imm=(symbolTable[target]-currAddr)/4;
            }
            else{
                try{
                    inst.imm=std::stoi(target,nullptr,0);
                }
                catch(...){
                    errors.push_back("Line "+std::to_string(lNum)+": Undefined label '"+target+"'"); 
                }
            }
            inst.isImm=true;
        }
        machineCode.push_back(encode(inst));
        currAddr+=4;
    }
    return machineCode;


}