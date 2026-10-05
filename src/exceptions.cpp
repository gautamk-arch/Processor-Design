#include <stdexcept>
#include <string>
class StackOverflow : public std::runtime_error
{
public:
    StackOverflow() : std::runtime_error("StackOverflow: Stack limit exceeded") {}
};

class StackUnderflow : public std::runtime_error
{
public:
    StackUnderflow() : std::runtime_error("StackUnderflow: Popped beyond stack base") {}
};

class IllegalInstruction : public std::runtime_error
{
public:
    IllegalInstruction() : std::runtime_error("IllegalInstruction: Invalid opcode") {}
};

class DivideByZero : public std::runtime_error
{
public:
    DivideByZero() : std::runtime_error("DivideByZero : Attempting to divide by zero") {}
};

class Misaligned : public std::runtime_error
{
public: 
    Misaligned() : std::runtime_error("Misaligned : Memory access not properly aligned") {}
};

class BadAddress : public std::runtime_error
{
public:
    BadAddress() : std::runtime_error("BadAddress : Memory access out of bounds") {}
};

class PCOutofRange : public std::runtime_error
{
public:
    PCOutofRange() : std::runtime_error("PCOutofRange : Program counter exceeded instruction limit") {}
};