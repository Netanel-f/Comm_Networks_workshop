CC = gcc
CCA = cc
CXX = g++

INCS=-I.
CFLAGS = -Wall -std=c11 -libverbs
#CFLAGS = -std=c11 -libverbs -D _GNU_SOURCE
CXXFLAGS = -Wall -std=c++11 -g $(INCS)


TAR = tar
TARFLAGS = -czvf
TARNAME = 201209806.tar.gz
TARSRCS = ${ALL} ${EXE} Makefile

EXE = kv_server kv_client kv_indexer nweb
ALL = kv_server.c kv_client.c kv_indexer.c kv_shared.h README nweb32.c

default: all

all: ${EXE}

kv_server: kv_server.o
	$(CC) $(CFLAGS) -o kv_server kv_server.o

kv_server.o: kv_server.c
	$(CC) $(CFLAGS) -c kv_server.c

kv_client: kv_client.o
	$(CC) $(CFLAGS) -o kv_client kv_client.o

kv_client.o: kv_client.c
	$(CC) $(CFLAGS) -c kv_client.c

kv_indexer: kv_indexer.o
	$(CC) $(CFLAGS) -o kv_indexer kv_indexer.o

kv_indexer.o: kv_indexer.c
	$(CC) $(CFLAGS) -c kv_indexer.c

nweb: nweb23.c
	$(CCA) -libverbs -O2 nweb23.c -o nweb

ex3: kv_server kv_client

.PHONY : clean
clean:
	$(RM) *.o  ${EXE} $(TARNAME) *~

tar:
	$(TAR) $(TARFLAGS) $(TARNAME) $(TARSRCS)
