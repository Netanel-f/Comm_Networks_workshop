#include <cstring>
//#include <string>
#include <vector>
#include <numeric>
#include <chrono>

#include "shared.h"

#define THROUGHPUT_FORMAT "%d\t%f\t%s\t"
#define LATENCY_FORMAT "%f\t%s\n"


using FpMilliseconds = std::chrono::duration<double, std::chrono::milliseconds::period>;
//using FpMilliseconds = std::chrono::duration<float, std::chrono::milliseconds::period>;
using FpSeconds = std::chrono::duration<double, std::chrono::seconds::period>;

class Client {
    int server_fd;
    char read_buffer[WARMPUP_PACKET_SIZE + 1] = "0";

public:
    //// C-tor
    explicit Client(const char * serverIP);

    //// client actions
    void warm_up(); //TODO delete me
    void measure_throughput(size_t packetSize);
    void measure_latency(size_t packetSize);
    void kill_client();

private:
    static void print_error(const std::string& function_name, int error_number);

};


Client::Client(const char * serverIP) {
    // setup sockets and structs
    struct sockaddr_in serverAddress;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        print_error("socket() error", errno);
    }

    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(PORT_NUMBER);

    int retVal = inet_pton(AF_INET, serverIP, &serverAddress.sin_addr);
    if (retVal <= 0) {
        print_error("inet_pton()", errno);
    }

    retVal = connect(server_fd, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
    if (retVal != 0) {
        print_error("connect()", errno);
    }
    // clear read_buffer
    bzero(this->read_buffer, WARMPUP_PACKET_SIZE + 1);
}


void Client::warm_up() {
    /* Set chrono clocks*/
    std::chrono::steady_clock::time_point warm_up_start_time = std::chrono::steady_clock::now();
    std::chrono::high_resolution_clock::time_point start_time;
    std::chrono::high_resolution_clock::time_point end_time;

    bool keepWarmUp = true;
    int cycles_counter = 0; // TODO remove

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

        ssize_t retVal = send(this->server_fd, &msg, msg_size, 0);
        if (DEBUG) { std::cout << "Latency-sent size: " << msg_size << std::endl; }
        if (retVal != msg_size) {
            print_error("send() failed", errno);
        }

        retVal = recv(this->server_fd, this->read_buffer, msg_size, 0);
        if (DEBUG) { std::cout << "Latency-received size: " << retVal << std::endl; }
        if (retVal < 0) {
            print_error("recv() failed", errno);
        }

        end_time = std::chrono::high_resolution_clock::now();

        currentCycleDuration = end_time - start_time;

        cycles_counter++;
        if (cycles_counter == 1) {
            rtt = currentCycleDuration;
            continue;
        }

        // calculating weighted average of rtt.
        auto currentRTT = 0.8 * rtt + 0.2 * currentCycleDuration;

        auto total_time = std::chrono::steady_clock::now() - warm_up_start_time;

        auto total_time_seconds = std::chrono::duration_cast<std::chrono::seconds>(total_time).count();

        if ((total_time_seconds > MIN_SECONDS_TO_WARMUP) && (currentRTT - rtt < (rtt / 100))) {
            // convergence detection: a minimal number to start with,
            // followed by iterations until the average changes less than 1% between iterations...
            keepWarmUp = false;
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
//    auto cycle_Mbits_transferred = cycle_bytes_transferred / BYTES_TO_MEGABITS;
    auto cycle_bits_transferred = cycle_bytes_transferred * BYTES_TO_BITS;

//    using FpSeconds = std::chrono::duration<float, std::chrono::seconds::period>;


    /* Init the packet message to send*/
    char msg[packetSize];
    memset(msg, 1, packetSize);

    for (int cycleIndex = 0; cycleIndex < RTT_NUM_OF_CYCLES; cycleIndex++) {
        cycleStartTime = std::chrono::high_resolution_clock::now();

        for (int packetIndex = 0; packetIndex < RTT_PACKETS_PER_CYCLE; packetIndex++) {
            ssize_t retVal = send(this->server_fd, &msg, packetSize, 0);
            if (retVal != packetSize) {
                print_error("send() failed", errno);
            }

            retVal = recv(this->server_fd, this->read_buffer, packetSize, 0);
            if (retVal < 0) {
                print_error("recv() failed", errno);
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

//    printf(THROUGHPUT_FORMAT, (int)packetSize, max_rate, "Megabits / second");
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

    printf(THROUGHPUT_FORMAT, (int)packetSize, max_rate, rate_unit.c_str());

}



void Client::measure_latency(size_t packetSize) {
    std::chrono::high_resolution_clock::time_point start_time;
    std::chrono::high_resolution_clock::time_point end_time;


//    using FpMilliseconds = std::chrono::duration<float, std::chrono::milliseconds::period>;

    //create message in size
    char msg[packetSize];
    memset(msg, 1, packetSize);

    //take time
    start_time = std::chrono::high_resolution_clock::now();

    ssize_t retVal = send(this->server_fd, &msg, packetSize, 0);
    if (DEBUG) { std::cout << "Latency-sent size: " << packetSize << std::endl; }
    if (retVal != packetSize) {
        print_error("send() failed", errno);
    }

    retVal = recv(this->server_fd, this->read_buffer, packetSize, 0);
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


/**
 * close the open sockets.
 */
void Client::kill_client() {
    int retVal = close(server_fd);
    if (DEBUG) { std::cout << "close output: " << retVal << std::endl; }
}


void Client::print_error(const std::string& function_name, int error_number) {
    printf("ERROR: %s %d.\n", function_name.c_str(), error_number);
    exit(EXIT_FAILURE);
}


int main(int argc, char const *argv[]) {
    Client client = Client(argv[1]);

    client.warm_up();    // warm up until latency converges

    for (size_t packetSize = 1; packetSize <= 1024; packetSize = packetSize << 1) {
        client.measure_throughput(packetSize);
        client.measure_latency(packetSize);
    }


    client.kill_client();
    return EXIT_SUCCESS;
}

