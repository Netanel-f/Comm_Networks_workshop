#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <stdbool.h>
#include <string.h>
#include <cstring>
#include <string>
#include <chrono>
#include <vector>
#include <numeric>

#include "shared.h"


void print_error(const std::string& function_name, int error_number) {
    printf("ERROR: %s %d.\n", function_name.c_str(), error_number);
    exit(EXIT_FAILURE);
}

class Client {
    int serverfd;

    char readBuf[WARMPUP_PACKET_SIZE + 1];
    char writeBuf[WARMPUP_PACKET_SIZE + 1];

public:
    //// C-tor
    Client();

    //// client actions
    void killClient();
    void warmup();

private:

};

Client::Client() {

    struct sockaddr_in clientAddress;
    struct sockaddr_in serverAddress;

    serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverfd < 0) {
        print_error("socket() error", errno);
    }

    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(PORT_NUMBER);

    int retVal = inet_pton(AF_INET, SERVER_ADDRESS, &serverAddress.sin_addr);
    if (retVal <= 0) {
        print_error("inet_pton()", errno);
    }

    retVal = connect(serverfd, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
    if (retVal != 0) {
        print_error("connect()", errno);
    }

    bzero(this->writeBuf, WARMPUP_PACKET_SIZE + 1);
}

void Client::killClient() {
    std::cout << "Bye, World!" << std::endl;
//    int retVal = close(serverfd);
    int retVal = shutdown(serverfd, SHUT_RDWR);
    std::cout << "close output: " << retVal << std::endl;

}

void Client::warmup() {
//    int sentMessages = 0;
    int receivedMessages = 0;

    std::chrono::high_resolution_clock clock;
    std::chrono::high_resolution_clock::time_point startTime;
    std::chrono::high_resolution_clock::time_point endTime;

    std::vector<std::chrono::high_resolution_clock::duration> durations;
    size_t num_of_messages = 0;
    bool keepWarmUp = true;
    int cycles_counter = 0;

    while (keepWarmUp) {
        std::cout << "warmup cycle #" << cycles_counter << std::endl;
        //create message in size
        char msg[WARMPUP_PACKET_SIZE];
        memset(msg, 1, WARMPUP_PACKET_SIZE);
        size_t msg_size = WARMPUP_PACKET_SIZE;

        //take time
        startTime = std::chrono::high_resolution_clock::now();

        for (int sent = 0; sent < PACKETS_PER_CYCLE; sent++) {
            //send msg
            ssize_t retVal = send(this->serverfd, &msg, msg_size, 0);
            if (retVal != msg_size) {
                print_error("send() failed", errno);
            }

            retVal = recv(this->serverfd, this->readBuf, (size_t) WARMPUP_PACKET_SIZE, 0);
            if (retVal < 0) {
                print_error("recv() failed", errno);
            }

        }

        endTime = std::chrono::high_resolution_clock::now();
        std::chrono::high_resolution_clock::duration currentCycleDuration = endTime - startTime;
        cycles_counter++;
        if (cycles_counter > 10) {
            std::chrono::high_resolution_clock::duration lastCycleDuration = durations.back();
            if (currentCycleDuration - lastCycleDuration < (lastCycleDuration / 100)) {
                keepWarmUp = false;
            }
        }

        durations.push_back(currentCycleDuration);
        num_of_messages += (2 * PACKETS_PER_CYCLE);

    }
//    for (int cycle_number = 0; cycle_number < MIN_WARMUP_CYCLES; cycle_number++){
//        std::cout << "warmup cycle #" << cycle_number << std::endl;
//        //create message in size
//        char msg[WARMPUP_PACKET_SIZE];
//        memset(msg, 1, WARMPUP_PACKET_SIZE);
//        size_t msg_size = WARMPUP_PACKET_SIZE;
//
//        //take time
//        startTime = std::chrono::high_resolution_clock::now();
//
//        for (int sent = 0; sent < PACKETS_PER_CYCLE; sent++) {
//            //send msg
//            ssize_t retVal = send(this->serverfd, &msg, msg_size, 0);
//            if (retVal != msg_size) {
//                print_error("send() failed", errno);
//            }
//
//            retVal = recv(this->serverfd, this->readBuf, (size_t) WARMPUP_PACKET_SIZE, 0);
//            if (retVal < 0) {
//                print_error("recv() failed", errno);
//            }
//
//        }
//
//        endTime = std::chrono::high_resolution_clock::now();
//
//        durations.push_back(endTime - startTime);
//        num_of_messages += (2 * PACKETS_PER_CYCLE);
////        auto totalTimeMS = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
////        std::cout << "totalTimeMS: " << totalTimeMS << std::endl;
//
//    }

//    auto bytes_transferred = MIN_WARMUP_CYCLES * num_of_messages * WARMPUP_PACKET_SIZE;
    auto bytes_transferred = cycles_counter * num_of_messages * WARMPUP_PACKET_SIZE;
    auto total_time = std::accumulate(durations.begin(), durations.end(),
            std::chrono::high_resolution_clock::duration(0));


    auto totalTimeMS = std::chrono::duration_cast<std::chrono::milliseconds>(total_time).count();
    std::cout << "totalTimeMS: " << totalTimeMS << std::endl;

    auto throughput = bytes_transferred / totalTimeMS;
    std::cout << "throughput: " << throughput << " bytes/millisecond" << std::endl;

    auto rtt = totalTimeMS / (MIN_WARMUP_CYCLES * PACKETS_PER_CYCLE);
    std::cout << "rtt: " << rtt << " totalTimeMS / (MIN_WARMUP_CYCLES * PACKETS_PER_CYCLE)" << std::endl;
    auto latency = rtt / 2;
    std::cout << "latency: " << latency << " milliseconds" << std::endl;
//    std::cout << "throughput: " << throughput << " bytes/millisecond" << std::endl;


}

int main(int argc, char const *argv[]) {
    Client client = Client();
    client.warmup();

    client.killClient();
    return 0;
}