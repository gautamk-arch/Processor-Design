#include "preprocessor.h"
#include <iostream>
#include <vector>
#include <string>

using namespace std;
struct testcase
{
    string desc;
    string input;
    string expected;
    int exp_no_Error;
};

void testCheck(string desc, bool iscorrect)
{
    if(iscorrect)
    {
        cout<<"Success : "<<desc<<"\n";
    }
    else
    {
        cout<<"Failed : "<<desc<<"\n";
    }
}

int main()
{
    cout<<"Hi, Running the Preprocessor tests here: \n";
    vector<testcase> tests;
    tests={
        {"Basic mov instruction","mov r1, 10","mov r1, 10\n", 0},
        {"Testing PUSH","push r1","\tsub sp,sp,4\n\tst r1, 0[sp]\n",0},
        {"Testing POP","pop r2","\tld r2, 0[sp]\n\tadd sp,sp,4\n",0},
        {"Testing case insensitivity","PuSh r3","\tsub sp,sp,4\n\tst r3, 0[sp]\n",0},
        {"Testing missing register","push","push\n",1},
        {"Testing full line '@' comment handling","@ this is a comment","@ this is a comment\n",0}
    };
    for (int i = 0; i < tests.size(); i++)
    {
        vector<string> err;
        string actual=expandMacros(tests[i].input,err);
        bool iscorrect=(actual==tests[i].expected) && (err.size()==tests[i].exp_no_Error);
        testCheck(tests[i].desc,iscorrect);
    }
    cout<<"All tests finished\n";
    
}