#pragma once
#include <string>
#include <vector>
#include "isa.h"

std::vector<string> assemble(const std::string& src,std::vector<std::string>& errors);