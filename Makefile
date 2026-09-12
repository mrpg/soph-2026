CXX = g++
CXXFLAGS = -O3 -Wall -Wextra -std=c++17
LDLIBS = -lgmpxx -lgmp

all: soph

soph: soph.cpp
	$(CXX) $(CXXFLAGS) -pthread -o $@ $< $(LDLIBS)

clean:
	rm -f soph

.PHONY: all clean
