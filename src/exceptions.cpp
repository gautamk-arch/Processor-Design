#include <stdexcept>
#include <string>
#include "exceptions.h"

StackOverflow::StackOverflow() : std::runtime_error("StackOverflow: Stack limit exceeded") {}

StackUnderflow::StackUnderflow() : std::runtime_error("StackUnderflow: Popped beyond stack base") {}

IllegalInstruction::IllegalInstruction() : std::runtime_error("IllegalInstruction: Invalid opcode") {}

DivideByZero::DivideByZero() : std::runtime_error("DivideByZero : Attempting to divide by zero") {}

Misaligned::Misaligned() : std::runtime_error("Misaligned : Memory access not properly aligned") {}

BadAddress::BadAddress() : std::runtime_error("BadAddress : Memory access out of bounds"){}

PCOutofRange::PCOutofRange() : std::runtime_error("PCOutofRange : Program counter exceeded instruction limit") {}