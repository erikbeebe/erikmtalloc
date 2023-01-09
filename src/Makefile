# gcc support tested with:
# g++ (Ubuntu 9.4.0-1ubuntu1~20.04.1) 9.4.0
CXX = g++
# clang++ support tested with:
# clang version 10.0.0-4ubuntu1 
# CXX = clang++
CC = $(CXX)

CXXFLAGS = -g -Wno-deprecated -ggdb -O0 -std=c++2a -pthread -Wpedantic
CFLAGS = $(CXXFLAGS)
 
default: tests

tests: erikmtalloc.o testcases.o overrides.o
	$(CC) $(CFLAGS) -o tests erikmtalloc.o testcases.o overrides.o

erikmtalloc: erikmtalloc.o
	$(CC) $(CFLAGS) erikmtalloc.o

testcases: testcases.o
	$(CC) $(CFLAGS) testcases.o

overrides: overrides.o
	$(CC) $(CFLAGS) overrides.o

clean:
	$(RM) erikmtalloc *.o
