#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "isa.h"

string disassembleWord(const string& hexWord);
vector<string> disassembleProgram(const vector<string>& hexProgram);
string formatInstruction(const Instruction& inst);

