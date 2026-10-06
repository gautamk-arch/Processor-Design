#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include "assembler.h"
#include "disassembler.h"
#include "preprocessor.h"
#include "cpu.h"

using namespace std;

int main(int argc, char* argv[]){

    // Arguments parsing
    if(argc != 2){
        cerr << "Error: Invalid argument\nCorrect Usage- " << argv[0] << " <assembly file name>" << endl;
        return 1;
    }
    string asmFileName = argv[1];

    // Reading source asm file
    ifstream asmFile(asmFileName);
    if(!asmFile.is_open()){
        cerr << "Error: Not able to open " << asmFileName << endl;
        return 1;
    }

    stringstream buffer;
    buffer << asmFile.rdbuf();
    string sourceCode = buffer.str();
    asmFile.close();

    // Passing the source code through assembler to generate hex file
    vector<string> errors;
    string expandedCode=expandMacros(sourceCode,errors);
    vector<string> assembled_code = assemble(expandedCode, errors);

    if(!errors.empty()){
        cerr << "--- Assembler errors ---\n";
        for (const auto& err: errors){
            cerr << err << "\n";
        }
        return 1;
    }

    // Generating hex file for test cases
    string name_of_file = asmFileName.substr(0,asmFileName.find_last_of('.'));
    ofstream hexFile(name_of_file + ".hex");
    for(const auto& hexLine: assembled_code){
        hexFile << hexLine << "\n";
    }
    hexFile.close();
    cout << "Successfully assembled to " << name_of_file << ".hex\n";
    
    // Generating dissembly file
    ofstream disFile(name_of_file + ".dis");
    vector<string> dissembled_code = disassembleProgram(assembled_code);
    for (const auto& disLine: dissembled_code){
        disFile << disLine << "\n";
    }
    disFile.close();
    cout << "Successfully disassembled to " << name_of_file << ".dis\n";

    // Executing in CPU
    CPU cpu;
    cpu.loadHex(assembled_code);
    cout <<"--- Starting Debugger ---\n";
    cout <<"Type 'help' to see commands, 'quit' to exit.\n";
    string line;
    while(true) {
        cout<<"debug> ";
        if(!getline(cin,line)) break;
        if(line.empty()) continue;

        stringstream ss(line);
        string cmd;
        ss>>cmd;
        try {
            if(cmd=="quit" || cmd=="q"){
                break;
            }
            else if(cmd=="run") {
                cpu.run();
            }
            else if(cmd=="step") {
                int steps=1;
                string arg;
                if(ss>>arg) steps=stoi(arg);

                for(int i=0;i<steps;i++)
                {
                    cpu.step();
                }
            }
            else if(cmd=="regs") {
                cpu.dumpRegisters();
            }
            else if(cmd=="stack") {
                cpu.printStack();
            }
            else if(cmd=="reset") {
                cpu.reset();
                cpu.loadHex(assembled_code);
                cout<<"CPU reset and program reloaded\n";
            }
            else if(cmd =="mem") {
                string addr_str, n_str;
                if(ss>>addr_str) {
                    int n = 1;
                    if(ss>>n_str) n = stoi(n_str);
                    try {
                        uint32_t addr = stoul(addr_str, nullptr, 16);
                        cpu.printMemory(addr, n);
                    } catch (...) {
                        cout << "Invalid memory address format.\n";
                    }
                } else {
                    cout << "Usage: mem <addr> [n]\n";
                }
            }
            else if(cmd=="break") {
                string target;
                if(ss>>target) {
                    try {
                        uint32_t b_addr=stoul(target,nullptr,16);
                        cpu.toggleBreakpoint(b_addr);
                    }
                    catch(...) {
                        cout<<"Invalid breakpoint address format. No label resolution found.\n";
                    }
                }
                else {
                    cout<<"Usage: break <addr>\n";
                }
            }
            else if(cmd=="pipe" || cmd=="micro") {
                cout<<"Command '"<<cmd<<"' is not yet implemented.\n";
            }
            else if(cmd=="help") {
                cout<<"Commands : \n"
                    <<" step n              - Executes n intructions(default = 1)\n"
                    <<" run                 - Executes until complete or break\n"
                    <<" break <label|addr>  - Sets a breakpoint\n"
                    <<" regs                - Dump CPU registers\n"
                    <<" mem <addr> n        - Views n words of memory at addr\n"
                    <<" stack               - Stack Visualizer\n"
                    <<" reset               - Reset CPU and reload program\n"
                    <<" pipe                - View pipeline state\n"
                    <<" micro               - View microarchitectural state\n"
                    <<" quit or q           - Exit debugger\n";
            }
            else {
                cout<<"Unknown command: "<<cmd<<"\n";
            }
        }
        catch(const exception& e) {
            cerr<<"Execution stopped: "<<e.what()<<"\n";
        }
    }
    return 0;
} 