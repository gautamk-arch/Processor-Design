#!/bin/sh
# Builds the trace exporter: every simulator source except main.cpp, plus tracer.cpp.
# usage: ./build_trace.sh   ->  ./simtrace
set -e
cd "$(dirname "$0")"
SRCS=$(ls *.cpp | grep -v '^main.cpp$')
g++ -std=c++17 -O2 -Wall -o simtrace $SRCS
echo "built ./simtrace"
