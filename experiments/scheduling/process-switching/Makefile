ANDROID := $(if $(shell echo '__ANDROID__' | $(CXX) -E - | grep __ANDROID__),,1)
CXXFLAGS = -g -O0 -Wall

ifdef ANDROID
CXXFLAGS += -lstdc++
else
CXXFLAGS += -lpthread -lrt
endif

all: process-switching

process-switching: process-switching.cpp
	$(CXX) -o $@ $^ $(CXXFLAGS)

clean:
	-rm process-switching
	-rm *~
