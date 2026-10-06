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
        else
        {
            expandedsrc+=line+"\n";
        }
        line_number++;
    }
    return expandedsrc;
}