//
// Created by netanel on 30/03/2019.
//

#ifndef COMM_NETS_SHARED_H
#define COMM_NETS_SHARED_H

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <stdio.h>
//#include <stdbool.h>
#include <string.h>
#include <chrono>   //TODO check me
#include <math.h>

#define MAX_INCOMING_QUEUE 1
#define PORT_NUMBER 54321
#define WARMPUP_PACKET_SIZE 1024
#define MIN_WARMUP_CYCLES 100
#define PACKETS_PER_CYCLE 1000

#define RTT_PACKETS_PER_CYCLE 1000
#define RTT_NUM_OF_CYCLES 100
#define BYTES_TO_MEGABITS 125000
#define BYTES_TO_BITS 8

#define KILOBIT_IN_BITS 1000
#define MEGABIT_IN_BITS 1000000
#define GIGABIT_IN_BITS 1000000000

#define LATENCY_MIN_WARM_PACKETS 1000

#define DEBUG true  // TODO delete before submission


#endif //COMM_NETS_SHARED_H
