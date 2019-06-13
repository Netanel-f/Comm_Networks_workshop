CC = gcc
CXX = g++

INCS=-I.
CFLAGS = -Wall -libverbs -D _GNU_SOURCE
CXXFLAGS = -Wall -std=c++11 -g $(INCS)


TAR = tar
TARFLAGS = -czvf
TARNAME = 201209806.tar.gz
TARSRCS = ${ALL} ${EXE} Makefile

CLIENT = shared.h client.cpp
SERVER = shared.h server.cpp
EXE = kv_template
ALL = kv_template.c README

default: all

all: ${EXE}

#client: client.o
#	$(CXX) $(CXXFLAGS) -o client client.o
#
#server: server.o
#	$(CXX) $(CXXFLAGS) -o server server.o
#
#client.o: shared.h client.cpp
#	$(CXX) $(CXXFLAGS) -c client.cpp
#
#server.o: shared.h server.cpp
#	$(CXX) $(CXXFLAGS) -c server.cpp
#
kv_template: kv_template.o
	$(CC) $(CFLAGS) -o kv_template kv_template.o

kv_template.o: kv_template.c
	$(CC) $(CFLAGS) -c kv_template.c


.PHONY : clean
clean:
	$(RM) *.o  ${EXE} $(TARNAME) *~

tar:
	$(TAR) $(TARFLAGS) $(TARNAME) $(TARSRCS)
