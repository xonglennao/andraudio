CXXFLAGS = -g -O0 -Wall -lpthread -lrt

all: process-switching

process-switching: process-switching.cpp
	$(CXX) -o $@ $^ $(CXXFLAGS)

clean:
	-rm process-switching
	-rm *~
