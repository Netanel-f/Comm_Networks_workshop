CC = gcc
CXX = g++

INCS=-I.
#CFLAGS = -Wall -std=c11 -libverbs -D _GNU_SOURCE
CFLAGS = -std=c11 -libverbs -D _GNU_SOURCE
CXXFLAGS = -Wall -std=c++11 -g $(INCS)


TAR = tar
TARFLAGS = -czvf
TARNAME = 201209806.tar.gz
TARSRCS = ${ALL} ${EXE} Makefile

EXE = kv_template
ALL = kv_template.c README

default: all

all: ${EXE}

kv_template: kv_template.o
	$(CC) $(CFLAGS) -o kv_template kv_template.o

kv_template.o: kv_template.c
	$(CC) $(CFLAGS) -c kv_template.c


.PHONY : clean
clean:
	$(RM) *.o  ${EXE} $(TARNAME) *~

tar:
	$(TAR) $(TARFLAGS) $(TARNAME) $(TARSRCS)
