#include "cpu.h"
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <string>
using std::cout;
using std::vector;
using std::string;

CPU::CPU(){
    instMem.resize(cap_of_inst_mem,0);
    dataMem.resize(cap_of_data_mem,0);
    reset();
}
 
void CPU::reset(){
    pc = 0;
    regs.fill(0);
    regs[sp]=stack_base;
    flag_E = false;
    flag_GT = false;
    instruction_limit = 0;
}

void CPU::loadHex(const vector<string> &hexLines){
    reset();
    size_t i = 0;
    for (const auto& line: hexLines){
        if (i>=cap_of_inst_mem) throw std::out_of_range("Instruction memory limit exceeded.");
        instMem[i] = std::stoul(line,nullptr,16);
        i++;
    }
    instruction_limit = i*4;
}

void CPU::checkMemoryAccess(uint32_t addr) const {
    if(addr%4!=0) throw Misaligned();
    if(addr+3>=cap_of_data_mem) throw BadAddress();
}

uint32_t CPU::fetchInst(){
    if(pc >= instruction_limit) throw PCOutofRange();
    return instMem[pc/4];
}

void CPU::run(){
    if(breakpoints.count(pc))
    {
        step();
    }
    while (pc < instruction_limit) {
        if(breakpoints.count(pc)) {
            cout<<"Breakpoint at PC: 0x"<<std::setfill('0')<<std::hex<<pc<<std::dec<<"\n";
            return; //pauses the execution and returns to CLI
        }
        step();
    }
    cout << "Execution stopped.\n";
}

void CPU::step(){ 
    if (pc >= instruction_limit) return;

    static uint32_t cycleCount=0;
    cycleCount++;
    uint32_t currPC=pc;
    uint32_t word;

    try{
        word=fetchInst();
        Instruction inst=decode(word);
        execute(inst);
    }
    catch(const std::exception &e) {
        std::cerr<<"\n------------------------------\n";
        std::cerr<<"HARDWARE EXCEPTION: "<<e.what()<<"\n";
        std::cerr<<"Cycle   :"<<std::dec<<cycleCount<<"\n";
        std::cerr<<"PC  : 0x"<<std::setfill('0')<<std::setw(8)<<std::hex<<currPC<<"\n";
        std::cerr<<"Instruction : 0x"<<std::setfill('0')<<std::setw(8)<<std::hex<<word<<"\n";
        std::cerr<<"------------------------------\n";

        pc=instruction_limit;
    }

}

void CPU::execute (Instruction inst){
    int32_t A = regs[inst.rs1];
    int32_t B = inst.isImm ? ImmValue(inst.mod, inst.imm) : regs[inst.rs2];
    uint32_t current_pc = pc;
    pc += 4;

    switch(inst.op){
        // Arithmetic instructions
        case Opcode::add: regs[inst.rd] = A + B; break; // temporary placeholders until ALU finishes. Also will help to check if CPU data path is correct.
        case Opcode::sub: regs[inst.rd] = A - B; break;
        case Opcode::mul: regs[inst.rd] = A * B; break;
        case Opcode::div: 
        if (B == 0) throw DivideByZero();
        regs[inst.rd] = A / B; 
        break;
        case Opcode::mod: 
        if (B == 0) throw DivideByZero();
        regs[inst.rd] = A % B; 
        break;

        // Compare instruction
        case Opcode::cmp:
        flag_E = (A==B);
        flag_GT = (A>B);
        break;

        // Logical instructions
        case Opcode::and_op: regs[inst.rd] = A & B; break;
        case Opcode::or_op: regs[inst.rd] = A | B; break;
        case Opcode::not_op: regs[inst.rd] = ~A ; break;

        // Move instruction
        case Opcode::mov: regs[inst.rd] = B ; break;

        // Shift instructions
        case Opcode::lsl: regs[inst.rd] = A << (B & 0x1F) ; break;
        case Opcode::lsr: regs[inst.rd] = static_cast<uint32_t>(A) >> (B & 0x1F) ; break;
        case Opcode::asr: regs[inst.rd] = A >> (B & 0x1F) ; break;

        // Nop instruction
        case Opcode::nop: break;

        // Load and store instructions. Assuming Little endian
        case Opcode::ld:{
            uint32_t addr = A + B;
            checkMemoryAccess(addr);

            if(inst.rs1==sp && addr>=stack_base) throw StackUnderflow();
            regs[inst.rd] = (dataMem[addr])|(dataMem[addr+1]<<8)|(dataMem[addr+2]<<16)|(dataMem[addr+3]<<24);
            break;
        }
        case Opcode::st:{
            uint32_t addr = A + B;
            checkMemoryAccess(addr);
            if (inst.rs1==sp && addr<stack_limit) throw StackOverflow();
            dataMem[addr] = regs[inst.rd] & 0xFF;
            dataMem[addr+1] = (regs[inst.rd]>>8) & 0xFF;
            dataMem[addr+2] = (regs[inst.rd]>>16) & 0xFF;
            dataMem[addr+3] = (regs[inst.rd]>>24) & 0xFF;
            break;
        }

        // Branch instructions
        case Opcode::b:
            pc = current_pc + (inst.imm * 4);
            break;
        case Opcode::beq:
            if(flag_E) pc = current_pc + (inst.imm * 4);
            break;
        case Opcode::bgt:
            if(flag_GT) pc = current_pc + (inst.imm * 4);
            break;
        
        case Opcode::call:
            regs[15] = pc;
            pc = current_pc + (inst.imm * 4);
            break;
        case Opcode::ret:
            pc = regs[15];
            break;
        default:
            throw IllegalInstruction(); // to handle opcodes from 21 to 31
    }
    // StackGuard - to check inst directly  writes to sp
    if(inst.rd==sp){
        if(regs[sp]<stack_limit) throw StackOverflow();
        if(regs[sp]>stack_base) throw StackUnderflow();
    }
}

void CPU::dumpRegisters() const{
    cout << "--- CPU STATE ---\n";

    cout << "PC : 0x" << std::setfill('0') << std::setw(8) << std::hex << pc << "\n";
    cout << "CMP : E=" << flag_E << " GT=" << flag_GT << "\n\n";

    for(int i=0; i<16; ++i){
        cout << "r" << std::dec << std::setw(2) << std::setfill(' ') << i << ": 0x" << std::setfill('0') << std::hex << std::setw(8) << regs[i] << "\t";
        if ((i+1)%4==0){
            cout << "\n";
        }
    }
    cout << std::dec << "-----------------\n";
}

void CPU::printStack() const{
    cout<<"\n---- ASCII STACK VISUALIZER ---\n";
    for(uint32_t addr =stack_base-4;addr>=stack_limit;addr-=4){
        cout<<"0x"<<std::setfill('0') << std::setw(8) <<std::hex <<addr << " : ";

        if(addr<regs[sp]) {
            cout<< "....";
        }
        else{
            uint32_t word=dataMem[addr] | (dataMem[addr+1]<<8) | (dataMem[addr+2]<<16) | (dataMem[addr+3]<<24);
            cout<<"0x"<<std::setfill('0')<<std::setw(8) <<std::hex<<word;
        }

        if(addr==regs[sp]){
            cout<<" <-- sp";
        }
        if(addr==stack_base-4){
            cout<<" [STACK BASE]";
        }
        cout<<"\n";
        if(addr==0) break;
    }
    cout<<std::dec<<"------------------------\n";
}

void CPU::toggleBreakpoint(uint32_t addr) {
    if(breakpoints.count(addr)) {
        breakpoints.erase(addr);
        cout<<"Breakpoint removed at 0x"<<std::setfill('0')<<std::setw(8)<<std::hex<<addr<<std::dec<<"\n";
    } else {
        breakpoints.insert(addr);
        cout<<"Breakpoint set at 0x"<<std::setfill('0')<<std::setw(8)<<std::hex<<addr<<std::dec<<"\n";
    }
}

void CPU::printMemory(uint32_t addr, int words) const {
    cout<<"\n--- Memory View ---\n";
    for(int i=0;i<words;i++) {
        uint32_t curr_addr =addr + i*4;

        if(curr_addr+3>=cap_of_data_mem) {
            cout<<"0x"<<std::setfill('0')<<std::setw(8)<<std::hex<<curr_addr<<" : Out of Bounds\n";
            break;
        }

        uint32_t word = dataMem[curr_addr] | (dataMem[curr_addr+1]<<8) | (dataMem[curr_addr+2]<<16) | (dataMem[curr_addr+3]<<24);
        cout << "0x" << std::setfill('0') << std::setw(8) << std::hex << curr_addr << " : 0x" << std::setfill('0') << std::setw(8) << std::hex << word << "\n";
    }
    cout<<std::dec<<"------------------------------\n";
}