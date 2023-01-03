CXX = g++
CC = $(CXX)

CXXFLAGS = -g -Wno-deprecated -ggdb -O0 -std=c++2a -pthread
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
