CC = g++
CXX = g++

INCS=-I.
CFLAGS = -Wall -std=c++11 -g $(INCS)
CXXFLAGS = -Wall -std=c++11 -g $(INCS)


TAR = tar
TARFLAGS = -czvf
TARNAME = 201209806.tar.gz
TARSRCS = ${ALL} ${EXE} Makefile

CLIENT = shared.h client.cpp
SERVER = shared.h server.cpp
EXE = client server
ALL = client.cpp server.cpp shared.h 

default: all

all: ${EXE}

client: client.o

server: server.o

client.o: ${CLIENT}

server.o: ${SERVER}



.PHONY : clean
clean:
	$(RM) *.o  ${EXE} $(TARNAME) *~

tar:
	$(TAR) $(TARFLAGS) $(TARNAME) $(TARSRCS)
