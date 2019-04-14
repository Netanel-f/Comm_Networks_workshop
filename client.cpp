#include <cstring>
//#include <string>
#include <vector>
#include <numeric>
#include <chrono>

#include "shared.h"

#define THROUGHPUT_FORMAT "%d\t%f\t%s\t"
#define LATENCY_FORMAT "%f\t%s\n"

using namespace std::chrono;
using fp_milliseconds = std::chrono::duration<double, std::chrono::milliseconds::period>;
using fp_seconds = std::chrono::duration<double, std::chrono::seconds::period>;

/**
 * Constructor of Client class.
 */
class Client {
    int server_fd;
    char read_buffer[WARMPUP_PACKET_SIZE + 1] = "0";

public:
    //// C-tor
    explicit Client(const char * serverIP);

    //// client actions
    void warm_up(); //TODO delete me
    void measure_throughput(size_t packet_size);
    void measure_latency(size_t packet_size);
    void kill_client();

private:
    static void print_error(const std::string& function_name, int error_number);

};

/**
 * Thie method will create Client Object and will connect the client to server
 * @param serverIP - the server destination ipv4 format.
 */
Client::Client(const char * serverIP) {
    /* setup sockets and structs */
    struct sockaddr_in serverAddress;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { print_error("socket() error", errno); }

    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(PORT_NUMBER);

    int ret_value = inet_pton(AF_INET, serverIP, &serverAddress.sin_addr);
    if (ret_value <= 0) { print_error("inet_pton()", errno); }

    ret_value = connect(server_fd, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
    if (ret_value != 0) { print_error("connect()", errno); }

    bzero(this->read_buffer, WARMPUP_PACKET_SIZE + 1);  // clear read_buffer
}

/**
 * Warm up the TCP/IP protocol to measure optimal results.
 */
void Client::warm_up() {
    /* Set chrono clocks*/
    steady_clock::time_point warm_up_start_time = steady_clock::now();
    steady_clock::time_point cycle_start_time, cycle_end_time;

    bool keepWarmUp = true;
    int cycles_counter = 0; // TODO remove

    auto rtt = fp_milliseconds(steady_clock::duration(0));

    steady_clock::duration current_cycle_duration;

    /* Create message in pre-defined warm-up packet size */
    char msg[WARMPUP_PACKET_SIZE];
    memset(msg, 1, WARMPUP_PACKET_SIZE);
    size_t msg_size = WARMPUP_PACKET_SIZE;

    /* Loop until RTT converges, but no less than MIN_SECONDS_TO_WARMUP */
    while (keepWarmUp) {
        if (DEBUG) { std::cout << "Latency cycle #" << cycles_counter << std::endl; }

        //take time
        cycle_start_time = steady_clock::now();

        ssize_t ret_value = send(this->server_fd, &msg, msg_size, 0);
        if (DEBUG) { std::cout << "Latency-sent size: " << msg_size << std::endl; }
        if (ret_value != msg_size) {
            print_error("send() failed", errno);
        }

        ret_value = recv(this->server_fd, this->read_buffer, msg_size, 0);
        if (DEBUG) { std::cout << "Latency-received size: " << ret_value << std::endl; }
        if (ret_value < 0) {
            print_error("recv() failed", errno);
        }

        cycle_end_time = steady_clock::now();

        /* Measure cycle RTT*/
        current_cycle_duration = cycle_end_time - cycle_start_time;

        cycles_counter++;
        if (cycles_counter == 1) {
            rtt = current_cycle_duration;
            continue;
        }

        /* Calculate weighted average of RTT. */
        auto currentRTT = 0.8 * rtt + 0.2 * current_cycle_duration;

//        auto total_time = cycle_end_time - warm_up_start_time; //TODO del
//        auto total_time_seconds = duration_cast<seconds>(total_time).count();

        auto total_time_seconds = duration_cast<seconds>(cycle_end_time - warm_up_start_time).count();

        if ((total_time_seconds > MIN_SECONDS_TO_WARMUP) && (currentRTT - rtt < (rtt / 100))) {
            // convergence detection: a minimal number to start with,
            // followed by iterations until the average changes less than 1% between iterations...
            keepWarmUp = false;
        }
    }
}


void Client::measure_throughput(size_t packet_size) {
    /* Set chrono clocks*/
    steady_clock::time_point cycle_start_time, cycle_end_time;
    steady_clock::duration cycleTime;
    double max_rate = 0.0;

    /* init calculations */
    auto cycle_bytes_transferred = 2* RTT_PACKETS_PER_CYCLE * packet_size;
    auto bits_transferred_per_cycle = cycle_bytes_transferred * BYTES_TO_BITS;

    /* Init the packet message to send*/
    char msg[packet_size];
    memset(msg, 1, packet_size);

    // Measure throughput for pre defined # of cycle
    for (int cycleIndex = 0; cycleIndex < RTT_NUM_OF_CYCLES; cycleIndex++) {
        cycle_start_time = steady_clock::now();

        // Sending continuously pre defined # of packets.
        for (int packetIndex = 0; packetIndex < RTT_PACKETS_PER_CYCLE; packetIndex++) {
            /* Send packet and verify the #bytes sent equal to #bytes requested to sent. */
            ssize_t ret_value = send(this->server_fd, &msg, packet_size, 0);
            if (ret_value != packet_size) { print_error("send() failed", errno); }

            /* Receive packet and verify the #bytes sent. */
            ret_value = recv(this->server_fd, this->read_buffer, packet_size, 0);
            if (ret_value < 0) { print_error("recv() failed", errno); }
        }

        cycle_end_time  = steady_clock::now();

        auto cycle_time_seconds = fp_seconds(cycle_end_time - cycle_start_time);

        auto cycle_throughput = bits_transferred_per_cycle / cycle_time_seconds.count();
        if (cycle_throughput > max_rate) {
            max_rate = cycle_throughput;
        }
    }

    /* Print maximal throughput measured */
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

    printf(THROUGHPUT_FORMAT, (int)packet_size, max_rate, rate_unit.c_str());

}



void Client::measure_latency(size_t packet_size) {
    /* Set chrono clocks*/
    steady_clock::time_point start_time;
    steady_clock::time_point end_time;



    /* Init the packet message to send*/
    char msg[packet_size];
    memset(msg, 1, packet_size);

    /* Take time*/
    start_time = steady_clock::now();

    /* Send 1 packet with (size_t) packet_size */
    ssize_t ret_value = send(this->server_fd, &msg, packet_size, 0);
    if (ret_value != packet_size) { print_error("send() failed", errno); }

    if (DEBUG) { std::cout << "Latency-sent size: " << packet_size << std::endl; }

    /* Receive 1 packet with (size_t) packet_size */
    ret_value = recv(this->server_fd, this->read_buffer, packet_size, 0);
    if (ret_value < 0) { print_error("recv() failed", errno); }

    if (DEBUG) { std::cout << "Latency-received size: " << ret_value << std::endl; }

    end_time = steady_clock::now();

    auto rtt = fp_milliseconds(steady_clock::duration(end_time - start_time));
    auto latency = rtt.count() / 2;
    if (DEBUG) { std::cout << "latency is: " << latency << " milliseconds." << std::endl; }

    printf(LATENCY_FORMAT, latency, "milliseconds");
}


/**
 * close the open socket towards server.
 */
void Client::kill_client() {
    close(server_fd);
}

/**
 * This method will print the function that raised the given error number.
 * @param function_name function that caused error
 * @param error_number errno that been raised.
 */
void Client::print_error(const std::string& function_name, int error_number) {
    printf("ERROR: %s %d.\n", function_name.c_str(), error_number);
    exit(EXIT_FAILURE);
}


int main(int argc, char const *argv[]) {

    /* Create client object and connect to given server-ip */
    Client client = Client(argv[1]);

    /* warm up until latency converges */
    client.warm_up();

    /* Measure throughput and latency , for exponential series of message sizes */
    for (size_t packet_size = 1; packet_size <= 1024; packet_size = packet_size << 1u) {
        client.measure_throughput(packet_size);
        client.measure_latency(packet_size);
    }

    /* Close client and disconnect from server */
    client.kill_client();
    return EXIT_SUCCESS;
}

