#include <cstring>
#include <string>
#include <vector>
#include <numeric>

#include "shared.h"

#define THROUGPUT_FORMAT "%d\t%f\t%s\t"
#define LATENCY_FORMAT "%f\t%s\n"

class Client {
    int serverfd;
    char readBuf[WARMPUP_PACKET_SIZE + 1] = "0";

public:
    //// C-tor
    explicit Client(const char * serverIP);

    //// client actions
    void kill_client();
    void warm_up(); //TODO delete me
    void measure_throughput(size_t packetSize);
    void measure_latency(size_t packetSize);
    void print_error(const std::string& function_name, int error_number);
//    void warm_up(size_t packetSize); //TODO delete me
//    void measureRTT(size_t packetSize);
//    void warmupLatency();

//private:

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

    bzero(this->readBuf, WARMPUP_PACKET_SIZE + 1);
}

void Client::kill_client() {
    int retVal = close(serverfd);
    if (DEBUG) { std::cout << "close output: " << retVal << std::endl; }
}


void Client::warm_up() {
    /* Set chrono clocks*/
    std::chrono::high_resolution_clock::time_point start_time;
    std::chrono::high_resolution_clock::time_point end_time;
    std::vector<std::chrono::high_resolution_clock::duration> durations;

    bool keepWarmUp = true;
    int cycles_counter = 0; // TODO remove

    using FpMilliseconds = std::chrono::duration<float, std::chrono::milliseconds::period>;
    auto rtt = FpMilliseconds(std::chrono::high_resolution_clock::duration(0));

    std::chrono::high_resolution_clock::duration currentCycleDuration;

    //create message in size
    char msg[WARMPUP_PACKET_SIZE];
    memset(msg, 1, WARMPUP_PACKET_SIZE);
    size_t msg_size = WARMPUP_PACKET_SIZE;

    while (keepWarmUp) {
        if (DEBUG) { std::cout << "Latency cycle #" << cycles_counter << std::endl; }

        //take time
        start_time = std::chrono::high_resolution_clock::now();

        ssize_t retVal = send(this->serverfd, &msg, msg_size, 0);
        if (DEBUG) { std::cout << "Latency-sent size: " << msg_size << std::endl; }
        if (retVal != msg_size) {
            print_error("send() failed", errno);
        }

        retVal = recv(this->serverfd, this->readBuf, msg_size, 0);
        if (DEBUG) { std::cout << "Latency-received size: " << retVal << std::endl; }
        if (retVal < 0) {
            print_error("recv() failed", errno);
        }

        end_time = std::chrono::high_resolution_clock::now();

        currentCycleDuration = end_time - start_time;
        durations.push_back(currentCycleDuration);

        cycles_counter++;

        // calculating weighted average of rtt.
        auto currentRTT = 0.8 * rtt + 0.2 * currentCycleDuration;


        auto total_time = std::accumulate(durations.begin(), durations.end(),
                                          std::chrono::high_resolution_clock::duration(0));

        auto total_time_seconds = std::chrono::duration_cast<std::chrono::seconds>(total_time).count();

        if ((total_time_seconds > 20) && (currentRTT - rtt < (rtt / 100))) {
            // convergence detection: a minimal number to start with,
            // followed by iterations until the average changes less than 1% between iterations...
            keepWarmUp = false;
        }

        rtt = currentRTT;
        if (DEBUG) { std::cout << "latency is: " << rtt.count() << " milliseconds." << std::endl; }

        if (msg[0] == 0) { //Todo check if needed
            memset(msg, 1, WARMPUP_PACKET_SIZE);
        } else {
            memset(msg, 0, WARMPUP_PACKET_SIZE);
        }
    }
}



void Client::measure_throughput(size_t packetSize) {
    /* Set chrono clocks*/
    std::chrono::high_resolution_clock::time_point cycleStartTime;
    std::chrono::high_resolution_clock::time_point cycleEndTime;
    std::chrono::high_resolution_clock::duration cycleTime;
    float max_rate = 0.0;

    // init calculations
    auto cycle_bytes_transferred = 2* RTT_PACKETS_PER_CYCLE * packetSize;
    auto cycle_Mbits_transferred = cycle_bytes_transferred / BYTES_TO_MEGABITS;
    auto cycle_bits_transferred = cycle_bytes_transferred * BYTES_TO_BITS;

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
                memset(msg, 1, packetSize);
            } else {
                memset(msg, 0, packetSize);
            }

        }

        cycleEndTime  = std::chrono::high_resolution_clock::now();
        cycleTime = (cycleEndTime - cycleStartTime);

        auto fptsecs = FpSeconds(cycleTime);
        auto totalTimeSecs = fptsecs.count();

//        auto cycleThroughput = cycle_Mbits_transferred / totalTimeSecs;
        auto cycleThroughput = cycle_bits_transferred / totalTimeSecs;
        if (cycleThroughput > max_rate) {
            max_rate = cycleThroughput;
        }
    }

//    printf(THROUGPUT_FORMAT, (int)packetSize, max_rate, "Megabits / second");
    std::string rate_unit;
    if (max_rate > GIGABIT_IN_BITS) {
        max_rate = max_rate / GIGABIT_IN_BITS;
        rate_unit = "Gbps";
    } else if (max_rate > MEGABIT_IN_BITS) {
        rate_unit = "Mbps";
        max_rate = max_rate / MEGABIT_IN_BITS;
    } else if (max_rate > KILOBIT_IN_BITS) {
        max_rate = max_rate / KILOBIT_IN_BITS;
        rate_unit = "Kbps";
    } else {
        rate_unit = "bps";
    }

    printf(THROUGPUT_FORMAT, (int)packetSize, max_rate, rate_unit.c_str());

}



void Client::measure_latency(size_t packetSize) {
    std::chrono::high_resolution_clock::time_point start_time;
    std::chrono::high_resolution_clock::time_point end_time;


    using FpMilliseconds = std::chrono::duration<float, std::chrono::milliseconds::period>;

    //create message in size
    char msg[packetSize];
    memset(msg, 1, packetSize);

    //take time
    start_time = std::chrono::high_resolution_clock::now();

    ssize_t retVal = send(this->serverfd, &msg, packetSize, 0);
    if (DEBUG) { std::cout << "Latency-sent size: " << packetSize << std::endl; }
    if (retVal != packetSize) {
        print_error("send() failed", errno);
    }

    retVal = recv(this->serverfd, this->readBuf, packetSize, 0);
    if (DEBUG) { std::cout << "Latency-received size: " << retVal << std::endl; }
    if (retVal < 0) {
        print_error("recv() failed", errno);
    }

    end_time = std::chrono::high_resolution_clock::now();

//    std::chrono::high_resolution_clock::duration currentCycleDuration = endTime - startTime;
    auto rtt = FpMilliseconds(std::chrono::high_resolution_clock::duration(end_time - start_time));
    auto latency = rtt.count() / 2;
    if (DEBUG) { std::cout << "latency is: " << latency << " milliseconds." << std::endl; }

    printf(LATENCY_FORMAT, latency, "milliseconds");
}




void Client::print_error(const std::string& function_name, int error_number) {
    printf("ERROR: %s %d.\n", function_name.c_str(), error_number);
    exit(EXIT_FAILURE);
}


// TODO fix print as requested
int main(int argc, char const *argv[]) {
    Client client = Client(argv[1]);

    client.warm_up();    // warm up until latency converges

    for (size_t packetSize = 1; packetSize <= 1024; packetSize = packetSize << 1) {
        client.measure_throughput(packetSize);
        client.measure_latency(packetSize);
    }


//    client.warmupLatency();

//    for (size_t packetSize = 1; packetSize <= 1024; packetSize = packetSize<<1) {
//        client.measureRTT(packetSize);
//    }

    client.kill_client();
    return EXIT_SUCCESS;
}


// Code to delete

////TODO delete me
//void Client::warmup(size_t packetSize) {
//    /* Set chrono clocks*/
//    std::chrono::high_resolution_clock clock;
//    std::chrono::high_resolution_clock::time_point startTime;
//    std::chrono::high_resolution_clock::time_point endTime;
//    std::chrono::high_resolution_clock::time_point rttStart;
//    std::chrono::high_resolution_clock::time_point rttEnd;
//
//    std::vector<std::chrono::high_resolution_clock::duration> durations;
//    std::vector<std::chrono::high_resolution_clock::duration> rttdurations;
//    size_t num_of_messages = 0;
//    bool keepWarmUp = true;
//    int cycles_counter = 0;
//
//    while (keepWarmUp) {
//        if (DEBUG) { std::cout << "warm_up cycle #" << cycles_counter << std::endl; }
//        //create message in size
//        char msg[packetSize];
//        memset(msg, 1, packetSize);
//        size_t msg_size = packetSize;
//
//        //take time
//        startTime = std::chrono::high_resolution_clock::now();
//
//        for (int sent = 0; sent < PACKETS_PER_CYCLE; sent++) {
//            //send msg
//            rttStart = std::chrono::high_resolution_clock::now();
//            ssize_t retVal = send(this->serverfd, &msg, msg_size, 0);
//            if (DEBUG) { std::cout << "warm_up sent size: " << msg_size<< std::endl; }
//            if (retVal != msg_size) {
//                print_error("send() failed", errno);
//            }
//
//            retVal = recv(this->serverfd, this->readBuf, packetSize, 0);
//            if (DEBUG) { std::cout << "warm_up recieved size: " << retVal<< std::endl; }
//            if (retVal < 0) {
//                print_error("recv() failed", errno);
//            }
//            rttEnd = std::chrono::high_resolution_clock::now();
//            rttdurations.push_back(rttEnd - rttStart);
//
//        }
//
//        endTime = std::chrono::high_resolution_clock::now();
//        std::chrono::high_resolution_clock::duration currentCycleDuration = endTime - startTime;
//        cycles_counter++;
//        if (cycles_counter > MIN_WARMUP_CYCLES) {
//            std::chrono::high_resolution_clock::duration lastCycleDuration = durations.back();
//            if (currentCycleDuration - lastCycleDuration < (lastCycleDuration / 100)) {
//                keepWarmUp = false;
//            }
//        }
//
//        if (msg[0] == 0) {//Todo check if needed
//            memset(msg, 1, WARMPUP_PACKET_SIZE);
//        } else {
//            memset(msg, 0, WARMPUP_PACKET_SIZE);
//        }
//
//        durations.push_back(currentCycleDuration);
//        num_of_messages += (2 * PACKETS_PER_CYCLE);
//
//    }
//
//    auto bytes_transferred = cycles_counter * num_of_messages * WARMPUP_PACKET_SIZE;
//    auto total_time = std::accumulate(durations.begin(), durations.end(),
//                                      std::chrono::high_resolution_clock::duration(0));
//
//    using FpMilliseconds = std::chrono::duration<float, std::chrono::milliseconds::period>;
//    auto fptms = FpMilliseconds(total_time);
//
//
//    auto totalTimeMillisec = std::chrono::duration_cast<std::chrono::milliseconds>(total_time).count();
//    auto totalTimeMicrosec = std::chrono::duration_cast<std::chrono::microseconds>(total_time).count();
//
//    auto throughput = bytes_transferred / totalTimeMillisec;
//
//    auto rtt = totalTimeMicrosec / (MIN_WARMUP_CYCLES * PACKETS_PER_CYCLE);   // TODO need to fix.
//    auto latency = rtt / 2;
//
//    if (DEBUG) {
//        std::cout << "fptms: " << fptms.count() << std::endl;
//
//
//        std::cout << "totalTimeMS: " << totalTimeMillisec << std::endl;
//        std::cout << "throughput: " << throughput << " bytes/millisecond" << std::endl;
//        std::cout << "rtt: " << rtt << " totalTimeMicrosec / (MIN_WARMUP_CYCLES * PACKETS_PER_CYCLE)"
//                  << std::endl;
//        std::cout << "latency: " << latency << " microseconds" << std::endl;
//    }
//
////    for (std::chrono::high_resolution_clock::duration cur : rttdurations) {
////        auto micros = std::chrono::duration_cast<std::chrono::microseconds>(cur);
////        std::cout << micros.count() << '\n';
////    }
//}
//
//void Client::measureRTT(size_t packetSize) {
//    /* Set chrono clocks*/
//    std::chrono::high_resolution_clock::time_point cycleStartTime;
//    std::chrono::high_resolution_clock::time_point cycleEndTime;
//    float max_rate = 0.0;
//
//    // init calculations
//    auto cycle_bytes_transferred = 2* RTT_PACKETS_PER_CYCLE * packetSize;
//    auto cycle_Mbits_transferred = cycle_bytes_transferred / BYTES_TO_MEGABITS;
//    using FpSeconds = std::chrono::duration<float, std::chrono::seconds::period>;
//
//
//    /* Init the packet message to send*/
//    char msg[packetSize];
//    memset(msg, 1, packetSize);
//
//    for (int cycleIndex = 0; cycleIndex < RTT_NUM_OF_CYCLES; cycleIndex++) {
//        cycleStartTime = std::chrono::high_resolution_clock::now();
//
//        for (int packetIndex = 0; packetIndex < RTT_PACKETS_PER_CYCLE; packetIndex++) {
//            ssize_t retVal = send(this->serverfd, &msg, packetSize, 0);
//            if (retVal != packetSize) {
//                print_error("send() failed", errno);
//            }
//
//            retVal = recv(this->serverfd, this->readBuf, packetSize, 0);
//            if (retVal < 0) {
//                print_error("recv() failed", errno);
//            }
//
//            if (msg[0] == 0) {//Todo check if needed
//                memset(msg, 1, packetSize);
//            } else {
//                memset(msg, 0, packetSize);
//            }
//
//        }
//
//        cycleEndTime  = std::chrono::high_resolution_clock::now();
//        std::chrono::high_resolution_clock::duration cycleTime = (cycleEndTime - cycleStartTime);
//        auto fptsecs = FpSeconds(cycleTime);
//        auto totalTimeSecs = fptsecs.count();
//        auto cycleThroughput = cycle_Mbits_transferred / totalTimeSecs;
//        if (cycleThroughput > max_rate) {
//            max_rate = cycleThroughput;
//        }
//    }
//
////    if (DEBUG) { std::cout << "Packet size: " << packetSize << "\t Maximal throughput is: " << max_rate << "\tMegabits / second" << std::endl; }
//    printf(THROUGPUT_FORMAT, (int)packetSize, max_rate, "Megabits / second");
//
//}
//
//void Client::warmupLatency() {
//    std::chrono::high_resolution_clock::time_point startTime;
//    std::chrono::high_resolution_clock::time_point endTime;
//
//
//    bool keepWarmUp = true;
//    int cycles_counter = 0;
//
//    using FpMilliseconds = std::chrono::duration<float, std::chrono::milliseconds::period>;
//    auto rtt = FpMilliseconds(std::chrono::high_resolution_clock::duration(0));
//
//    //create message in size
//    char msg[WARMPUP_PACKET_SIZE];
//    memset(msg, 1, WARMPUP_PACKET_SIZE);
//    size_t msg_size = WARMPUP_PACKET_SIZE;
//
//    while (keepWarmUp) {
//        if (DEBUG) { std::cout << "Latency cycle #" << cycles_counter << std::endl; }
//
//        //take time
//        startTime = std::chrono::high_resolution_clock::now();
//
//        ssize_t retVal = send(this->serverfd, &msg, msg_size, 0);
//        if (DEBUG) { std::cout << "Latency-sent size: " << msg_size << std::endl; }
//        if (retVal != msg_size) {
//            print_error("send() failed", errno);
//        }
//
//        retVal = recv(this->serverfd, this->readBuf, msg_size, 0);
//        if (DEBUG) { std::cout << "Latency-received size: " << retVal << std::endl; }
//        if (retVal < 0) {
//            print_error("recv() failed", errno);
//        }
//
//        endTime = std::chrono::high_resolution_clock::now();
//
//        if (msg[0] == 0) { //Todo check if needed
//            memset(msg, 1, WARMPUP_PACKET_SIZE);
//        } else {
//            memset(msg, 0, WARMPUP_PACKET_SIZE);
//        }
//        std::chrono::high_resolution_clock::duration currentCycleDuration = endTime - startTime;
//
//        if (cycles_counter == 0) {
//            rtt = currentCycleDuration;
//        }
//
//        cycles_counter++;
//
//        // calculating weighted average of rtt.
//        auto currentRTT = 0.8 * rtt + 0.2 * currentCycleDuration;
//
//        if (cycles_counter > LATENCY_MIN_WARM_PACKETS) {
//            // convergence detection: a minimal number to start with,
//            // followed by iterations until the average changes less than 1% between iterations...
//            if (currentRTT - rtt < (rtt / 100)) {
//                keepWarmUp = false;
//            }
//        }
//        rtt = currentRTT;
//        if (DEBUG) { std::cout << "rtt is: " << rtt.count() << " milliseconds." << std::endl; }
//    }
//}

