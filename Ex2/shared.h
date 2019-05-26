
#ifndef COMM_NETS_SHARED_H
#define COMM_NETS_SHARED_H

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <map>
#include <chrono>

using namespace std::chrono;

#define MAX_INCOMING_QUEUE 1
#define PORT_NUMBER 54323
#define WARMUP_PACKET_SIZE 1024
#define MAX_PACKET_SIZE_BYTES 1048576

#define MAX_PARALLEL_STREAMS 10

#define MIN_SECONDS_TO_WARMUP 20

#define RTT_PACKETS_PER_CYCLE 1000
#define RTT_NUM_OF_CYCLES 100
#define BYTES_TO_BITS 8


#define ONE_BYTE 1
#define ONE_MEGABYTE_IN_BYTES 1048576


#define KILOBIT_IN_BITS 1000
#define MEGABIT_IN_BITS 1000000
#define GIGABIT_IN_BITS 1000000000

//#define RESULTS_FORMAT "%ld\t%f\t%s\t%.3f\t%s\t%f\t%s\n"
// msg size\t #sockets\t #threads\t total latency\t total throughput\t total packet rate
#define RESULTS_HEADER "Msg size:\t#Sockets:\t#Threads:\tTotal latency:\tTotal throughput:\tTotal packet rate:\n"
#define RESULTS_FORMAT "%ld\t%u\t%d\t%f\t%s\t%.3f\t%s\t%f\t%s\n"

#define DEBUG false

#endif //COMM_NETS_SHARED_H
