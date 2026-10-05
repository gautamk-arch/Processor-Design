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
    try{
        cpu.loadHex(assembled_code);
        cout << "--- Starting Execution ---\n";
        cpu.run();
        cout << "--- Execution completed successfully ---\n";
    }
    catch(const exception& e){
        cerr << "CPU runtime error: " << e.what() << "\n";
    }

    // Final state of machine
    cout << "\n";
    cpu.dumpRegisters();

    return 0;
}