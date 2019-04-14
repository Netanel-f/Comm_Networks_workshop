#include <cstring>
//#include <string>
#include <vector>
#include <numeric>
#include <chrono>

#include "shared.h"


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
    void run_tests();
    void kill_client();

private:
    void warm_up();
    void measure_throughput(char * msg, ssize_t packet_size);
    void measure_latency(char * msg, ssize_t packet_size);
    void print_results(ssize_t packet_size);
    //// Keeping private variables to store results.
//    char message[MAX_PACKET_SIZE];
//    ssize_t packet_size;
    double max_rate_result;
    double latency_result;


    static void print_error(const std::string& function_name, int error_number);

};

/**
 * Thie method will create Client Object and will connect the client to server
 * @param serverIP - the server destination ipv4 format.
 */
Client::Client(const char * serverIP) {
    /* setup sockets and structs */
    struct sockaddr_in server_address;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { print_error("socket() error", errno); }

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(PORT_NUMBER);

    int ret_value = inet_pton(AF_INET, serverIP, &server_address.sin_addr);
    if (ret_value <= 0) { print_error("inet_pton()", errno); }

    ret_value = connect(server_fd, (struct sockaddr *)&server_address, sizeof(server_address));
    if (ret_value != 0) { print_error("connect()", errno); }

    bzero(this->read_buffer, WARMPUP_PACKET_SIZE + 1);  // clear read_buffer
}

/**
 * Warm up the TCP/IP protocol to measure optimal results.
 */
void Client::warm_up() {
    char msg[WARMPUP_PACKET_SIZE];
    measure_latency(msg, WARMPUP_PACKET_SIZE);

    double rtt = this->latency_result;

    /* Set chrono clocks*/
    steady_clock::time_point warm_up_start_time = steady_clock::now();

    while (true) {
        measure_latency(msg, WARMPUP_PACKET_SIZE);

        auto warmup_seconds = duration_cast<seconds>(steady_clock::now() - warm_up_start_time).count();

        if ((warmup_seconds > MIN_SECONDS_TO_WARMUP) &&
            (this->latency_result - rtt < (rtt / 100))) {
            // convergence detection: a minimal number to start with,
            // followed by iterations until the average changes less than 1% between iterations...
            break;
        }
    }
}


void Client::measure_throughput(char * msg, ssize_t packet_size) {
    /* Set chrono clocks*/
    steady_clock::time_point cycle_start_time, cycle_end_time;
    steady_clock::duration cycleTime;
    this->max_rate_result = 0.0;

    /* init calculations */
    auto cycle_bytes_transferred = 2* RTT_PACKETS_PER_CYCLE * packet_size;
    auto bits_transferred_per_cycle = cycle_bytes_transferred * BYTES_TO_BITS;

    /* Init the packet message to send*/
    memset(msg, 1, packet_size);


    /* Measure throughput for pre defined # of cycle */
    for (int cycle_index = 0; cycle_index < RTT_NUM_OF_CYCLES; cycle_index++) {
        cycle_start_time = steady_clock::now();

        /* Sending continuously pre defined # of packets */
        for (int packet_index = 0; packet_index < RTT_PACKETS_PER_CYCLE; packet_index++) {
            /* Send packet and verify the #bytes sent equal to #bytes requested to sent. */
            ssize_t ret_value = send(this->server_fd, msg, packet_size, 0);
            if (ret_value != packet_size) { print_error("send() failed", errno); }

            /* Receive packet and verify the #bytes sent. */
            ret_value = recv(this->server_fd, this->read_buffer, packet_size, 0);
            if (ret_value < 0) { print_error("recv() failed", errno); }
        }

        cycle_end_time  = steady_clock::now();

        auto cycle_time_seconds = fp_seconds(cycle_end_time - cycle_start_time);

        auto cycle_throughput = bits_transferred_per_cycle / cycle_time_seconds.count();

        if (cycle_throughput > this->max_rate_result) {
            this->max_rate_result = cycle_throughput;
        }
    }
}


void Client::measure_latency(char * msg, ssize_t packet_size) {
    /* Set chrono clocks*/
    steady_clock::time_point start_time;
    steady_clock::time_point end_time;

    /* Init the packet message to send*/
    memset(msg, 1, packet_size);

    /* Take time*/
    start_time = steady_clock::now();

    /* Send 1 packet with (size_t) packet_size */
    ssize_t ret_value = send(this->server_fd, msg, packet_size, 0);
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

    this->latency_result = latency;
}

/**
 * This method will print results to screen as saved in client's object.
 * @param packet_size the packet size results.
 */
void Client::print_results(ssize_t packet_size) {
    /* Print maximal throughput measured */
    std::string rate_unit;
    if (max_rate_result > GIGABIT_IN_BITS) {
        max_rate_result = max_rate_result / GIGABIT_IN_BITS;
        rate_unit = "Gbps";
    } else if (max_rate_result > MEGABIT_IN_BITS) {
        rate_unit = "Mbps";
        max_rate_result = max_rate_result / MEGABIT_IN_BITS;
    } else if (max_rate_result > KILOBIT_IN_BITS) {
        max_rate_result = max_rate_result / KILOBIT_IN_BITS;
        rate_unit = "Kbps";
    } else {
        rate_unit = "bps";
    }

    printf(RESULTS_FORMAT, packet_size, max_rate_result, rate_unit.c_str(), latency_result, "milliseconds");
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

void Client::run_tests() {
    /* warm up until latency converges */
    warm_up();


    /* Measure throughput and latency , for exponential series of message sizes */
    for (ssize_t packet_size = 1; packet_size <= 1024; packet_size = packet_size << 1u) {
        /* Init the packet message to send*/
        char msg[packet_size];
        measure_throughput(msg, packet_size);
        measure_latency(msg, packet_size);
        print_results(packet_size);
    }
}


int main(int argc, char const *argv[]) {

    /* Create client object and connect to given server-ip and run tests */
    Client client = Client(argv[1]);
    client.run_tests();

    /* Close client and disconnect from server */
    client.kill_client();
    return EXIT_SUCCESS;
}

