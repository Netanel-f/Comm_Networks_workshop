
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

#define MAX_INCOMING_QUEUE 10
#define PORT_NUMBER 54321
#define WARMUP_PACKET_SIZE 1024
#define MAX_PACKET_SIZE_BYTES 1048576

#define MAX_PARALLEL_STREAMS 10
#define MIN_PARALLEL_STREAMS 1

#define MIN_SECONDS_TO_WARMUP 5

#define RTT_PACKETS_PER_CYCLE 100
#define RTT_NUM_OF_CYCLES 10
#define BYTES_TO_BITS 8


#define ONE_BYTE 1
#define MEGABYTE_IN_KILOBYTES 1024
#define MEGABYTE_IN_BYTES 1048576


#define KILOBIT_IN_BITS 1000
#define MEGABIT_IN_BITS 1000000
#define GIGABIT_IN_BITS 1000000000

// msg size\t #sockets\t #threads\t total latency\t total throughput\t total packet rate
#define RESULTS_FORMAT "%ld\t%s\t%u\t%d\t%f\t%s\t%.3f\t%s\t%Lf\t%s\n"
#define SAVE_RESULTS_TO_CSV true //set true when submit

#define DEBUG false

#endif //COMM_NETS_SHARED_H
