CXX = g++
CXXFLAGS = -g -Wall -D_REENTRANT

fib : fib.o fj.o
	$(CXX) -o $@ fib.o fj.o -lpthread

clean :
	rm -f fib *.o
