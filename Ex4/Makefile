CC = gcc
CXX = g++

INCS=-I.
#CFLAGS = -Wall -std=c11 -libverbs -D _GNU_SOURCE
#CFLAGS = -std=c11 -libverbs -D _GNU_SOURCE
CFLAGS = -std=c11 -libverbs
CXXFLAGS = -Wall -std=c++11 -g $(INCS)


TAR = tar
TARFLAGS = -czvf
TARNAME = 201209806.tar.gz
TARSRCS = ${ALL} ${EXE} Makefile

EXE = server client
ALL = kv_server.c kv_client.c kv_shared.c kv_shared.h README

default: all

all: ${EXE}

server: kv_server.o
	$(CC) $(CFLAGS) -o kv_server kv_server.o

kv_server.o: kv_server.c kv_shared.h kv_shared.c
	$(CC) $(CFLAGS) -c kv_server.c kv_shared.h kv_shared.c

client: kv_client.o
	$(CC) $(CFLAGS) -o kv_client kv_client.o

kv_client.o: kv_client.c kv_shared.h kv_shared.c
	$(CC) $(CFLAGS) -c kv_client.c kv_shared.h kv_shared.c


.PHONY : clean
clean:
	$(RM) *.o  ${EXE} $(TARNAME) *~

tar:
	$(TAR) $(TARFLAGS) $(TARNAME) $(TARSRCS)
