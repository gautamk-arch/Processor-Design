#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <chrono>
#include <memory>
#include "assembler.h"
#include "disassembler.h"
#include "preprocessor.h"
#include "cpu.h"
#include "pipeline.h"

using namespace std;

int main(int argc, char *argv[])
{

    // Arguments parsing
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " [-d|--debug] [--pipe4|--pipe6] [--hmicro|--vmicro] <filename.asm>\n";
        return 1;
    }

    string asmFileName = "";
    bool use_debugger = false;
    bool use_pipe4 = false;
    bool use_pipe6 = false;
    bool use_hmicro = false;
    bool use_vmicro = false;

    for (int i = 1; i < argc; i++)
    {
        string arg = argv[i];
        if (arg == "-d" || arg == "--debug")
        {
            use_debugger = true;
        }
        else if (arg == "--pipe4")
        {
            use_pipe4 = true;
        }
        else if (arg == "--pipe6")
        {
            use_pipe6 = true;
        }
        else if (arg == "--pipe")
        {
            cerr << "Error: Use --pipe4 (4-stage) or --pipe6 (6-stage).\n";
            return 1;
        }
        else if (arg == "--hmicro")
        {
            use_hmicro = true;
        }
        else if (arg == "--vmicro")
        {
            use_vmicro = true;
        }
        else if (arg.size() > 1 && arg[0] == '-')
        {
            // catches typos
            cerr << "Error: Unknown option '"<< arg <<"'.\n";
            return 1;
        }
        else
        {
            asmFileName = arg;
        }
    }

    // Flag combination rules
    if (use_hmicro && use_vmicro)
    {
        cerr << "Error: --hmicro and --vmicro cannot be used together. Choose one.\n";
        return 1;
    }
    if (use_pipe4 && use_pipe6)
    {
        cerr << "Error: --pipe4 and --pipe6 cannot be used together. Choose one.\n";
        return 1;
    }
    if ((use_hmicro || use_vmicro) && !(use_pipe4 || use_pipe6))
    {
        std::cerr << "Error: --hmicro / --vmicro require --pipe4 or --pipe6 (e.g. " << argv[0] << " --pipe6 --vmicro <filename.asm>).\n";
        return 1;
    }

    if (asmFileName.empty())
    {
        std::cerr << "Error: No input file specified.\n";
        return 1;
    }

    // Reading source asm file
    ifstream asmFile(asmFileName);
    if (!asmFile.is_open())
    {
        cerr << "Error: Not able to open " << asmFileName << endl;
        return 1;
    }

    stringstream buffer;
    buffer << asmFile.rdbuf();
    string sourceCode = buffer.str();
    asmFile.close();

    // Passing the source code through assembler to generate hex file
    vector<string> errors;
    string expandedCode = expandMacros(sourceCode, errors);
    vector<string> assembled_code = assemble(expandedCode, errors);

    if (!errors.empty())
    {
        cerr << "--- Assembler errors ---\n";
        for (const auto &err : errors)
        {
            cerr << err << "\n";
        }
        return 1;
    }

    // Generating hex file for test cases
    string name_of_file = asmFileName.substr(0, asmFileName.find_last_of('.'));
    ofstream hexFile(name_of_file + ".hex");
    for (const auto &hexLine : assembled_code)
    {
        hexFile << hexLine << "\n";
    }
    hexFile.close();
    cout << "Successfully assembled to " << name_of_file << ".hex\n";

    // Generating dissembly file
    ofstream disFile(name_of_file + ".dis");
    vector<string> dissembled_code = disassembleProgram(assembled_code);
    for (const auto &disLine : dissembled_code)
    {
        disFile << disLine << "\n";
    }
    disFile.close();
    cout << "Successfully disassembled to " << name_of_file << ".dis\n";

    // Architecture Configuration
    CPU cpu;
    std::unique_ptr<PipelineBase> pipeline; // owns the selected pipeline
    PipelineBase *activeCore = nullptr; // stays null in non-pipelined mode

    if (use_pipe4 || use_pipe6)
    {
        if (use_pipe6)
            pipeline = std::make_unique<PipelinedCore<6>>();
        else
            pipeline = std::make_unique<PipelinedCore<4>>();
        activeCore = pipeline.get();

        const char *depth = use_pipe6 ? "6-Stage" : "4-Stage";
        
        if (use_hmicro)
        {
            // Horizontal microprogrammed pipeline
            cout << "\n[Mode: Horizontal Microprogrammed " << depth << " Pipelined CPU]\n";
            activeCore->isMicroprogrammed = true;
            activeCore->useVertical = false;
        }
        else if (use_vmicro)
        {
            // Vertical microprogrammed pipeline
            cout << "\n[Mode: Vertical Microprogrammed " << depth << " Pipelined CPU]\n";
            activeCore->isMicroprogrammed = true;
            activeCore->useVertical = true;
        }
        else
        {
            // Hardwired pipeline (no microprogramming)
            cout << "\n[Mode: Hardwired " << depth << " Pipelined CPU]\n";
            activeCore->isMicroprogrammed = false;
            activeCore->useVertical = false;
        }
    }

    // Hardwired Single-Cycle CPU 
    // (Triggers if no flags are provided: ./simulator)
    else
    {
        cout << "\n[Mode: Single-Cycle Hardwired CPU]\n";
    }

    // Load initial memory state into the base CPU
    cpu.loadHex(assembled_code);
    if (activeCore)
        activeCore->reset();

    // Unified Execution and Debugger Loop
    if (use_debugger)
    {
        cout << "--- Starting Debugger ---\n";
        cout << "Type 'help' to see commands, 'quit' to exit.\n";
        string line;
        while (true)
        {
            cout << "debug> ";
            if (!getline(cin, line))
                break;
            if (line.empty())
                continue;

            stringstream ss(line);
            string cmd;
            ss >> cmd;
            try
            {
                if (cmd == "quit" || cmd == "q")
                {
                    break;
                }
                else if (cmd == "run")
                {
                    auto start_time = std::chrono::high_resolution_clock::now();

                    if (activeCore)
                        activeCore->run(cpu);
                    else
                        cpu.run();

                    auto end_time = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
                    cout << "[Run command completed in " << elapsed.count() << " ms]\n";
                }
                else if (cmd == "step")
                {
                    int steps = 1;
                    string arg;
                    if (ss >> arg)
                        steps = stoi(arg);

                    for (int i = 0; i < steps; i++)
                    {
                        if (activeCore)
                            activeCore->step(cpu);
                        else
                            cpu.step();
                    }
                }
                else if (cmd == "regs")
                {
                    cpu.dumpRegisters();
                }
                else if (cmd == "stack")
                {
                    cpu.printStack();
                }
                else if (cmd == "reset")
                {
                    cpu.reset();
                    if (activeCore)
                        activeCore->reset();
                    cpu.loadHex(assembled_code);
                    cout << "CPU reset and program reloaded\n";
                }
                else if (cmd == "mem")
                {
                    string addr_str, n_str;
                    if (ss >> addr_str)
                    {
                        int n = 1;
                        if (ss >> n_str)
                            n = stoi(n_str);
                        try
                        {
                            uint32_t addr = stoul(addr_str, nullptr, 16);
                            cpu.printMemory(addr, n);
                        }
                        catch (...)
                        {
                            cout << "Invalid memory address format.\n";
                        }
                    }
                    else
                    {
                        cout << "Usage: mem <addr> [n]\n";
                    }
                }
                else if (cmd == "break")
                {
                    string target;
                    if (ss >> target)
                    {
                        try
                        {
                            uint32_t b_addr = stoul(target, nullptr, 16);
                            cpu.toggleBreakpoint(b_addr);
                        }
                        catch (...)
                        {
                            cout << "Invalid breakpoint address format. No label resolution found.\n";
                        }
                    }
                    else
                    {
                        cout << "Usage: break <addr>\n";
                    }
                }
                else if (cmd == "pipe")
                {
                    if (activeCore)
                    {
                        activeCore->printPipelineDiagram();
                    }
                    else
                    {
                        cout << "Pipeline state is not available in Single-Cycle mode.\n";
                    }
                }
                else if (cmd == "micro")
                {
                    if (activeCore && activeCore->isMicroprogrammed)
                    {
                        cout << "Micro-PC: " << activeCore->currentMicroPC << "\n";
                    }
                    else
                    {
                        cout << "Microarchitectural state is only available in --micro mode.\n";
                    }
                }
                else if (cmd == "help")
                {
                    cout << "Commands : \n"
                         << " step n              - Executes n intructions(default = 1)\n"
                         << " run                 - Executes until complete or break\n"
                         << " break <label|addr>  - Sets a breakpoint\n"
                         << " regs                - Dump CPU registers\n"
                         << " mem <addr> n        - Views n words of memory at addr\n"
                         << " stack               - Stack Visualizer\n"
                         << " reset               - Reset CPU and reload program\n"
                         << " pipe                - View pipeline state\n"
                         << " micro               - View microarchitectural state\n"
                         << " quit or q           - Exit debugger\n";
                }
                else
                {
                    cout << "Unknown command: " << cmd << "\n";
                }
            }
            catch (const exception &e)
            {
                cerr << "Execution stopped: " << e.what() << "\n";
            }
        }
    }
    else
    {
        cout << "--- Starting Execution ---\n";
        try
        {
            auto start_time = chrono::high_resolution_clock::now();

            if (activeCore)
                activeCore->run(cpu);
            else
                cpu.run();

            auto end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> elapsed = end_time - start_time;

            cout << "--- Execution completed successfully in " << elapsed.count() << " ms ---\n";
        }
        catch (const exception &e)
        {
            cerr << "CPU runtime error: " << e.what() << "\n";
        }
        cout << "\n";
        cpu.dumpRegisters();
    }
    return 0;
}