#include "preprocessor.h"
#include <sstream>
#include <algorithm>
#include <cctype>
using namespace std;

string expandMacros(const string &src, vector<string> &errors)
{
    istringstream stream(src);
    string line;
    string expandedsrc="";
    int line_number=1;

    while(getline(stream,line))
    {
        istringstream linestream(line);
        string inst;
        linestream >> inst;
        if(inst =="" || inst[0] == '@')
        {
            expandedsrc+=line+"\n";
            line_number++;
            continue;
        }

        //since assembly is usually case insensitive so converting the instruction to lowercase
        string lowinst="";
        for(int i=0;i<inst.length();i++)
        {
            lowinst+=tolower(inst[i]);
        }

        if(lowinst == "push" || lowinst=="pop")
        {
            string reg;
            linestream>>reg;

            //if there is any comment written by the user
            int commentstartpos =reg.find('@');
            if(commentstartpos != string::npos)
            {
                reg=reg.substr(0,commentstartpos);
            }

            if(reg=="")
            {
                string err="Error at line number " + to_string(line_number) + ": No register found";
                errors.push_back(err);
                expandedsrc+=line+"\n";
            }
            else
            {
                if(lowinst=="push")
                {
                    expandedsrc+="\tsub sp,sp,4\n";
                    expandedsrc+="\tst " + reg + ", 0[sp]\n";
                }
                else if(lowinst=="pop")
                {
                    expandedsrc+="\tld "+reg+", 0[sp]\n";
                    expandedsrc+="\tadd sp,sp,4\n";
                }
            }
        }
        //expand the movf macro
        else if(lowinst == "movf")
        {
            string operands;
            getline(linestream, operands);
            
            // Remove any trailing comments
            size_t commentstartpos = operands.find('@');
            if(commentstartpos != string::npos)
            {
                operands = operands.substr(0, commentstartpos);
            }
            
            // Strip all spaces 
            operands.erase(remove_if(operands.begin(), operands.end(), ::isspace), operands.end());
            
            size_t commaPos = operands.find(',');
            if (commaPos == string::npos || operands.empty())
            {
                string err = "Error at line number " + to_string(line_number) + ": Invalid movf format (expected movf reg, imm)";
                errors.push_back(err);
                expandedsrc += line + "\n";
            }
            else
            {
                string reg = operands.substr(0, commaPos);
                string immStr = operands.substr(commaPos + 1);
                
                try
                {
                    // Parse as 64-bit to prevent overflow, base 0 handles 0x IEEE 754 formats flawlessly
                    unsigned long long fullImm = stoull(immStr, nullptr, 0);
                    unsigned int upper16 = (fullImm >> 16) & 0xFFFF;
                    unsigned int lower16 = fullImm & 0xFFFF;
                    
                    // Replace the macro with the two  hardware instructions
                    expandedsrc += "\tmovh " + reg + ", " + to_string(upper16) + "\n";
                    expandedsrc += "\toru " + reg + ", " + reg + ", " + to_string(lower16) + "\n";
                }
                catch (...)
                {
                    string err = "Error at line number " + to_string(line_number) + ": Invalid immediate value in movf";
                    errors.push_back(err);
                    expandedsrc += line + "\n";
                }
            }
        }
        else
        {
            expandedsrc+=line+"\n";
        }
        line_number++;
    }
    return expandedsrc;
}