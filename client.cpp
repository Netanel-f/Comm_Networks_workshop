#include <cstring>
#include <string>
//#include <chrono>
#include <vector>
#include <numeric>

#include "shared.h"



class Client {
    int serverfd;

    char readBuf[WARMPUP_PACKET_SIZE + 1] = "0";
    char writeBuf[WARMPUP_PACKET_SIZE + 1] = "0";

public:
    //// C-tor
    explicit Client(const char * serverIP);

    //// client actions
    void killClient();
    void warmup(size_t packetSize);
    void measureRTT(size_t packetSize);
    void warmupLatency();
    void print_error(const std::string& function_name, int error_number);

private:

};


Client::Client(const char * serverIP) {
    struct sockaddr_in serverAddress;

    serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverfd < 0) {
        print_error("socket() error", errno);
    }

    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(PORT_NUMBER);

    int retVal = inet_pton(AF_INET, serverIP, &serverAddress.sin_addr);
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
    if (DEBUG) { std::cout << "Bye, World!" << std::endl; }
    int retVal = close(serverfd);
    if (DEBUG) { std::cout << "close output: " << retVal << std::endl; }

}
void Client::warmupLatency() {
    std::chrono::high_resolution_clock::time_point startTime;
    std::chrono::high_resolution_clock::time_point endTime;

    bool keepWarmUp = true;
    int cycles_counter = 0;

    using FpMilliseconds = std::chrono::duration<float, std::chrono::milliseconds::period>;
    auto rtt = FpMilliseconds(std::chrono::high_resolution_clock::duration(0));

    //create message in size
    char msg[WARMPUP_PACKET_SIZE];
    memset(msg, 1, WARMPUP_PACKET_SIZE);
    size_t msg_size = WARMPUP_PACKET_SIZE;

    while (keepWarmUp) {
        if (DEBUG) { std::cout << "warmupLatency cycle #" << cycles_counter << std::endl; }

        //take time
        startTime = std::chrono::high_resolution_clock::now();

        ssize_t retVal = send(this->serverfd, &msg, msg_size, 0);
        if (DEBUG) { std::cout << "warmup sent size: " << msg_size << std::endl; }
        if (retVal != msg_size) {
            print_error("send() failed", errno);
        }

        retVal = recv(this->serverfd, this->readBuf, msg_size, 0);
        if (DEBUG) { std::cout << "warmup recieved size: " << retVal << std::endl; }
        if (retVal < 0) {
            print_error("recv() failed", errno);
        }

        endTime = std::chrono::high_resolution_clock::now();

        if (msg[0] == 0) {//Todo check if needed
            memset(msg, 1, WARMPUP_PACKET_SIZE);
        } else {
            memset(msg, 0, WARMPUP_PACKET_SIZE);
        }
        std::chrono::high_resolution_clock::duration currentCycleDuration = endTime - startTime;

        if (cycles_counter == 0) {
            rtt = currentCycleDuration;
        }

        cycles_counter++;

        // calculating weighted average of rtt.
        auto currentRTT = 0.8 * rtt + 0.2 * currentCycleDuration;

        if (cycles_counter > LATENCY_MIN_WARM_PACKETS) {
            // TODO need to check average
            // convergence detection: a minimal number to start with,
            // followed by iterations until the average changes less than 1% between iterations...
            if (currentRTT - rtt < (rtt / 100)) {
                keepWarmUp = false;
            }
        }
        rtt = currentRTT;
        if (DEBUG) { std::cout << "latency is: " << rtt.count() << " milliseconds." << std::endl; }
    }
}

void Client::warmup(size_t packetSize) {
    /* Set chrono clocks*/
    std::chrono::high_resolution_clock clock;
    std::chrono::high_resolution_clock::time_point startTime;
    std::chrono::high_resolution_clock::time_point endTime;
    std::chrono::high_resolution_clock::time_point rttStart;
    std::chrono::high_resolution_clock::time_point rttEnd;

    std::vector<std::chrono::high_resolution_clock::duration> durations;
    std::vector<std::chrono::high_resolution_clock::duration> rttdurations;
    size_t num_of_messages = 0;
    bool keepWarmUp = true;
    int cycles_counter = 0;

    while (keepWarmUp) {
        if (DEBUG) { std::cout << "warmup cycle #" << cycles_counter << std::endl; }
        //create message in size
        char msg[packetSize];
        memset(msg, 1, packetSize);
        size_t msg_size = packetSize;

        //take time
        startTime = std::chrono::high_resolution_clock::now();

        for (int sent = 0; sent < PACKETS_PER_CYCLE; sent++) {
            //send msg
            rttStart = std::chrono::high_resolution_clock::now();
            ssize_t retVal = send(this->serverfd, &msg, msg_size, 0);
            if (DEBUG) { std::cout << "warmup sent size: " << msg_size<< std::endl; }
            if (retVal != msg_size) {
                print_error("send() failed", errno);
            }

            retVal = recv(this->serverfd, this->readBuf, packetSize, 0);
            if (DEBUG) { std::cout << "warmup recieved size: " << retVal<< std::endl; }
            if (retVal < 0) {
                print_error("recv() failed", errno);
            }
            rttEnd = std::chrono::high_resolution_clock::now();
            rttdurations.push_back(rttEnd - rttStart);

        }

        endTime = std::chrono::high_resolution_clock::now();
        std::chrono::high_resolution_clock::duration currentCycleDuration = endTime - startTime;
        cycles_counter++;
        if (cycles_counter > MIN_WARMUP_CYCLES) {
            std::chrono::high_resolution_clock::duration lastCycleDuration = durations.back();
            if (currentCycleDuration - lastCycleDuration < (lastCycleDuration / 100)) {
                keepWarmUp = false;
            }
        }

        if (msg[0] == 0) {//Todo check if needed
            memset(msg, 1, WARMPUP_PACKET_SIZE);
        } else {
            memset(msg, 0, WARMPUP_PACKET_SIZE);
        }

        durations.push_back(currentCycleDuration);
        num_of_messages += (2 * PACKETS_PER_CYCLE);

    }

    auto bytes_transferred = cycles_counter * num_of_messages * WARMPUP_PACKET_SIZE;
    auto total_time = std::accumulate(durations.begin(), durations.end(),
            std::chrono::high_resolution_clock::duration(0));

    using FpMilliseconds = std::chrono::duration<float, std::chrono::milliseconds::period>;
    auto fptms = FpMilliseconds(total_time);


    auto totalTimeMillisec = std::chrono::duration_cast<std::chrono::milliseconds>(total_time).count();
    auto totalTimeMicrosec = std::chrono::duration_cast<std::chrono::microseconds>(total_time).count();

    auto throughput = bytes_transferred / totalTimeMillisec;

    auto rtt = totalTimeMicrosec / (MIN_WARMUP_CYCLES * PACKETS_PER_CYCLE);   // TODO need to fix.
    auto latency = rtt / 2;

    if (DEBUG) {
        std::cout << "fptms: " << fptms.count() << std::endl;


        std::cout << "totalTimeMS: " << totalTimeMillisec << std::endl;
        std::cout << "throughput: " << throughput << " bytes/millisecond" << std::endl;
        std::cout << "rtt: " << rtt << " totalTimeMicrosec / (MIN_WARMUP_CYCLES * PACKETS_PER_CYCLE)"
                  << std::endl;
        std::cout << "latency: " << latency << " microseconds" << std::endl;
    }

//    for (std::chrono::high_resolution_clock::duration cur : rttdurations) {
//        auto micros = std::chrono::duration_cast<std::chrono::microseconds>(cur);
//        std::cout << micros.count() << '\n';
//    }
}

void Client::measureRTT(size_t packetSize) {
    /* Set chrono clocks*/
    std::chrono::high_resolution_clock::time_point cycleStartTime;
    std::chrono::high_resolution_clock::time_point cycleEndTime;
//    std::vector<std::chrono::high_resolution_clock::duration> durations;
    float max_rate = 0.0;

    // init calculations
    auto bytes_transferred = RTT_NUM_OF_CYCLES * RTT_PACKETS_PER_CYCLE* packetSize;
    auto Mbits_transferred = 80000000 * bytes_transferred;
    using FpSeconds = std::chrono::duration<float, std::chrono::seconds::period>;


    /* Init the packet message to send*/
    char msg[packetSize];
    memset(msg, 1, packetSize);

    for (int cycleIndex = 0; cycleIndex < RTT_NUM_OF_CYCLES; cycleIndex++) {
        cycleStartTime = std::chrono::high_resolution_clock::now();

        for (int packetIndex = 0; packetIndex < RTT_PACKETS_PER_CYCLE; packetIndex++) {
            ssize_t retVal = send(this->serverfd, &msg, packetSize, 0);
            if (retVal != packetSize) {
                print_error("send() failed", errno);
            }

            retVal = recv(this->serverfd, this->readBuf, packetSize, 0);
            if (retVal < 0) {
                print_error("recv() failed", errno);
            }

            if (msg[0] == 0) {//Todo check if needed
                memset(msg, 1, WARMPUP_PACKET_SIZE);
            } else {
                memset(msg, 0, WARMPUP_PACKET_SIZE);
            }

        }

        cycleEndTime  = std::chrono::high_resolution_clock::now();
//        durations.push_back(cycleEndTime - cycleStartTime;)
        std::chrono::high_resolution_clock::duration cycleTime = (cycleEndTime - cycleStartTime);
        auto fptsecs = FpSeconds(cycleTime);
        auto totalTimeSecs = fptsecs.count();
        auto cycleThroughput = Mbits_transferred / totalTimeSecs;
        if (cycleThroughput > max_rate) {
            max_rate = cycleThroughput;
        }
    }

//    auto bytes_transferred = RTT_NUM_OF_CYCLES * RTT_PACKETS_PER_CYCLE* packetSize;
//    auto Mbits_transferred = 80000000 * bytes_transferred;

//    auto total_time = std::accumulate(durations.begin(), durations.end(),
//                                      std::chrono::high_resolution_clock::duration(0));

//    using FpSeconds = std::chrono::duration<float, std::chrono::seconds::period>;
//    auto fptsecs = FpSeconds(total_time);

//    auto totalTimeSecs = fptsecs.count();
//
//    auto throughput = Mbits_transferred / totalTimeSecs;
//
//    if (DEBUG) { std::cout << "throughput is: " << throughput << " Megabits / second" << std::endl; }
    if (DEBUG) { std::cout << "Maximal throughput is: " << max_rate << " Megabits / second" << std::endl; }

}

void Client::print_error(const std::string& function_name, int error_number) {
    printf("ERROR: %s %d.\n", function_name.c_str(), error_number);
    exit(EXIT_FAILURE);
}

int main(int argc, char const *argv[]) {
    Client client = Client(argv[1]);
    client.warmupLatency();
//    client.warmup(WARMPUP_PACKET_SIZE);
//    for (size_t packetSize = 1; packetSize <= 1024; packetSize = packetSize<<1) {
//        client.warmup(packetSize);
//    }

    for (size_t packetSize = 1; packetSize <= 1024; packetSize = packetSize<<1) {
        client.measureRTT(packetSize);
    }

    client.killClient();
    return EXIT_SUCCESS;
}