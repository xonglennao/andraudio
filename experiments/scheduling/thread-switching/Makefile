ANDROID := $(if $(shell echo '__ANDROID__' | $(CXX) -E - | grep __ANDROID__),,1)
CPPFLAGS = -g -O0 -Wall -lm

ifdef ANDROID
CXXFLAGS += -lstdc++
else
CPPFLAGS += -lpthread -lrt
endif

all: thread-switching roundtrip

clean:
	-rm thread-switching roundtrip
	-rm *~
