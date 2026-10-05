#pragma once
#include <stdexcept>
#include <string>

class StackOverflow : public std::runtime_error
{
public:
    StackOverflow();
};

class StackUnderflow : public std::runtime_error
{
public:
    StackUnderflow();
};

class IllegalInstruction : public std::runtime_error
{
public:
    IllegalInstruction();
};

class DivideByZero : public std::runtime_error
{
public:
    DivideByZero();
};

class Misaligned : public std::runtime_error
{
public: 
    Misaligned();
};

class BadAddress : public std::runtime_error
{
public:
    BadAddress();
};

class PCOutofRange : public std::runtime_error
{
public:
    PCOutofRange();
};