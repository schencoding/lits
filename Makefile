CXX = g++
CXXFLAGS = -std=c++14 -march=native -w -g -O3

all: example testbench

example: example.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

testbench: testbench.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -f example testbench 
