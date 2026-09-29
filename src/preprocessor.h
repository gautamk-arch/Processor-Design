#pragma once
#include <string>
#include <vector>
using namespace std;

string expandMacros(const string &src, vector<string> &errors);
//This is used to take read only source code and returns the modified source code 
//if there is any mistakes it goes to the error list