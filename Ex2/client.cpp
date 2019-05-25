
#include <chrono>
#include "shared.h"


using namespace std::chrono;
using fp_milliseconds = std::chrono::duration<double, std::chrono::milliseconds::period>;
using fp_seconds = std::chrono::duration<double, std::chrono::seconds::period>;


struct TCPSocket {
    int sockfd = -1;
    double max_throughput_result = 0.0;
    double packet_rate_result = 0.0;
    double latency_result = 0.0;
};

/**
 * Constructor of Client class.
 */
class Client {
    TCPSocket server_sockets[MAX_PARALLEL_STREAMS];
//    int server_fds[MAX_PARALLEL_STREAMS] = {-1};
//    int server_fd;  //todo remove
    char read_buffer[WARMPUP_PACKET_SIZE + 1] = "0";
    unsigned int num_of_streams = 1;

public:
    //// C-tor
//    explicit Client(const char * serverIP); //todo remove
    explicit Client(const char * serverIP, unsigned int num_of_streams);
    //// client actions
    void run_tests(bool incremental_msg_size);
    void kill_client();

private:
    static void print_error(const std::string& function_name, int error_number);
//    void warm_up();
    void warm_up(TCPSocket * tcpSocket);
    void measure_throughput(char * msg, ssize_t packet_size);
    void calculate_packet_rate(ssize_t packet_size);
    void measure_latency(TCPSocket * tcpSocket, char * msg, ssize_t packet_size);
    void print_results(ssize_t packet_size);
    //// Keeping private variables to store results.
    double max_throughput_result = 0.0;
    double packet_rate_result = 0.0;
    double latency_result = 0.0;
};

///**
// * Thie method will create Client Object and will connect the client to server
// * @param serverIP - the server destination ipv4 format.
// */
//Client::Client(const char * serverIP) {
//    /* setup sockets and structs */
//    struct sockaddr_in server_address;
//
//    server_fd = socket(AF_INET, SOCK_STREAM, 0);
//    if (server_fd < 0) { print_error("socket() error", errno); }
//
//    memset(&server_address, 0, sizeof(server_address));
//    server_address.sin_family = AF_INET;
//    server_address.sin_port = htons(PORT_NUMBER);
//
//    int ret_value = inet_pton(AF_INET, serverIP, &server_address.sin_addr);
//    if (ret_value <= 0) { print_error("inet_pton()", errno); }
//
//    ret_value = connect(server_fd, (struct sockaddr *)&server_address, sizeof(server_address));
//    if (ret_value != 0) { print_error("connect()", errno); }
//
//    bzero(this->read_buffer, WARMPUP_PACKET_SIZE + 1);  // clear read_buffer
//}


/**
 * Thie method will create Client Object and will connect the client to server
 * @param serverIP - the server destination ipv4 format.
 */
Client::Client(const char * serverIP, unsigned int num_of_streams) {

    if (num_of_streams > MAX_PARALLEL_STREAMS) {
        print_error("num of streams is over the limit", 1);
    }
//    server_fd = 0;  //todo remove
    this->num_of_streams = num_of_streams;
    /* setup sockets and structs */
    struct sockaddr_in server_address;

    // todo do we need different port numbers?
    for (unsigned int stream_idx = 0; stream_idx < num_of_streams; stream_idx++) {
        bool socket_creation_failed = false;

        int current_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (current_socket < 0) {
            print_error("socket() error", errno);
            socket_creation_failed = true;
        }

        server_sockets[stream_idx].sockfd = current_socket;
//        server_fds[stream_idx] = current_socket;

        memset(&server_address, 0, sizeof(server_address));
        server_address.sin_family = AF_INET;
        server_address.sin_port = htons(PORT_NUMBER);

        int ret_value = inet_pton(AF_INET, serverIP, &server_address.sin_addr);
        if (ret_value <= 0) {
            print_error("inet_pton()", errno);
            socket_creation_failed = true;
        }

        ret_value = connect(current_socket, (struct sockaddr *)&server_address, sizeof(server_address));
        if (ret_value != 0) {
            print_error("connect()", errno);
            socket_creation_failed = true;
        }

        if (socket_creation_failed) {
            stream_idx--;
        }
    }


    //todo maybe put this buffer in the struct?
    bzero(this->read_buffer, WARMPUP_PACKET_SIZE + 1);  // clear read_buffer
}

/**
 * Warm up the TCP/IP protocol to measure optimal results.
 */
void Client::warm_up(TCPSocket * tcpSocket) {
    if (DEBUG) { printf("DEBUG: %s\n", "warm up"); }

    char msg[WARMPUP_PACKET_SIZE];
    measure_latency(tcpSocket, msg, WARMPUP_PACKET_SIZE);

    double rtt = tcpSocket->latency_result;
//    double rtt = this->latency_result;

    /* Set chrono clocks*/
    steady_clock::time_point warm_up_start_time = steady_clock::now();

    while (true) {
        if (DEBUG) { printf("DEBUG: %s\n", "warm up while loop"); }
        measure_latency(tcpSocket, msg, WARMPUP_PACKET_SIZE);

        auto warmup_seconds = duration_cast<seconds>(steady_clock::now() - warm_up_start_time).count();

        if ((warmup_seconds > MIN_SECONDS_TO_WARMUP)&&(tcpSocket->latency_result - rtt < (rtt / 100))) {
            // convergence detection: a minimal number to start with,
            // followed by iterations until the average changes less than 1% between iterations...
            break;
        }
    }
}

/**
 * This method will measure throughput of TCP socket using message with certain size to send.
 * @param msg pointer to char[packet_size]
 * @param packet_size the size of message
 */
void Client::measure_throughput(char * msg, ssize_t packet_size) {
    /* Set chrono clocks*/
    steady_clock::time_point cycle_start_time, cycle_end_time;
    steady_clock::duration cycleTime;
//    tcpSocket->max_throughput_result = 0.0;
    this->max_throughput_result = 0.0;

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

            for (unsigned int stream_idx = 0; stream_idx < num_of_streams; stream_idx++) {   //todo
                /* Send packet and verify the #bytes sent equal to #bytes requested to sent. */
//            ssize_t ret_value = send(this->server_fd, msg, packet_size, 0);
                ssize_t ret_value = send(this->server_sockets[stream_idx].sockfd, msg, packet_size, 0);
                if (ret_value != packet_size) { print_error("send() failed", errno); }

                /* Receive packet and verify the #bytes sent. */
//            ret_value = recv(this->server_fd, this->read_buffer, packet_size, 0);
                ret_value = recv(this->server_sockets[stream_idx].sockfd, this->read_buffer, packet_size, 0);
                if (ret_value < 0) { print_error("recv() failed", errno); }
            }
//            /* Send packet and verify the #bytes sent equal to #bytes requested to sent. */
////            ssize_t ret_value = send(this->server_fd, msg, packet_size, 0);
//            ssize_t ret_value = send(tcpSocket->sockfd, msg, packet_size, 0);
//            if (ret_value != packet_size) { print_error("send() failed", errno); }
//
//            /* Receive packet and verify the #bytes sent. */
////            ret_value = recv(this->server_fd, this->read_buffer, packet_size, 0);
//            ret_value = recv(tcpSocket->sockfd, this->read_buffer, packet_size, 0);
//            if (ret_value < 0) { print_error("recv() failed", errno); }
        }

        cycle_end_time  = steady_clock::now();

        auto cycle_time_seconds = fp_seconds(cycle_end_time - cycle_start_time);

        auto cycle_throughput = bits_transferred_per_cycle / cycle_time_seconds.count();

        if (cycle_throughput > this->max_throughput_result) {
//        if (cycle_throughput > tcpSocket->max_throughput_result) {
//            tcpSocket->max_throughput_result = cycle_throughput;
            this->max_throughput_result = cycle_throughput;
        }
    }
}

/**
 * This method will calculate the packet rate, based on previously throughput measured.
 */
void Client::calculate_packet_rate(ssize_t packet_size) {
    //todo
    this->packet_rate_result =  (this->max_throughput_result) / packet_size;
}

/**
 * This method will measure latency of TCP socket using message with certain size to send.
 * @param msg pointer to char[packet_size]
 * @param packet_size the size of message
 */
void Client::measure_latency(TCPSocket * tcpSocket, char * msg, ssize_t packet_size) {
    /* Reset latency_result variable */
//    tcpSocket->latency_result = 0.0;
//    this->latency_result = 0.0;
    if (DEBUG) { printf("DEBUG: %s\n", "measure latency"); }

    /* Set chrono clocks*/
    steady_clock::time_point start_time, end_time;

    /* Init the packet message to send*/
    memset(msg, 1, packet_size);

    /* Take time*/
    start_time = steady_clock::now();

    /* Send 1 packet with (size_t) packet_size */
//    ssize_t ret_value = send(this->server_fd, msg, packet_size, 0);
//    ssize_t ret_value = send(sockfd, msg, packet_size, 0);
    ssize_t ret_value = send(tcpSocket->sockfd, msg, packet_size, 0);
    if (ret_value != packet_size) { print_error("send() failed", errno); }

    /* Receive 1 packet with (size_t) packet_size */
//    ret_value = recv(sockfd, this->read_buffer, packet_size, 0);
//    ret_value = recv(this->server_fd, this->read_buffer, packet_size, 0);
    ret_value = recv(tcpSocket->sockfd, this->read_buffer, packet_size, 0);
    if (ret_value < 0) { print_error("recv() failed", errno); }

    end_time = steady_clock::now();

    auto rtt = fp_milliseconds(steady_clock::duration(end_time - start_time));
    auto latency = rtt.count() / 2;
//    this->latency_result = latency;
    tcpSocket->latency_result = latency;
}

/**
 * This method will print results to screen as saved in client's object.
 * @param packet_size the packet size results.
 */
void Client::print_results(ssize_t packet_size) {
    /* Print maximal throughput measured */
    std::string rate_unit;
    if (max_throughput_result > GIGABIT_IN_BITS) {
        max_throughput_result = max_throughput_result / GIGABIT_IN_BITS;
        rate_unit = "Gbps";
    } else if (max_throughput_result > MEGABIT_IN_BITS) {
        rate_unit = "Mbps";
        max_throughput_result = max_throughput_result / MEGABIT_IN_BITS;
    } else if (max_throughput_result > KILOBIT_IN_BITS) {
        max_throughput_result = max_throughput_result / KILOBIT_IN_BITS;
        rate_unit = "Kbps";
    } else {
        rate_unit = "bps";
    }

    // calc total latency:
    for (unsigned int stream_idx = 0; stream_idx < this->num_of_streams; stream_idx++) {
        this->latency_result += this->server_sockets[stream_idx].latency_result;
    }

    this->latency_result /= this->num_of_streams;

//    printf(RESULTS_FORMAT, packet_size, max_throughput_result, rate_unit.c_str(), packet_rate_result, "packets/second", latency_result, "milliseconds");
    // msg size\t #sockets\t #threads\t total latency\t total throughput\t total packet rate
    printf(RESULTS_FORMAT, packet_size, this->num_of_streams, 1, latency_result, "milliseconds", max_throughput_result, rate_unit.c_str(), packet_rate_result, "packets/second");
}

/**
 * close the open socket towards server.
 */
void Client::kill_client() {
//    close(server_fd);

    for (unsigned int stream_idx = 0; stream_idx < this->num_of_streams; stream_idx++) {
        shutdown(this->server_sockets[stream_idx].sockfd, SHUT_RDWR);
        close(this->server_sockets[stream_idx].sockfd);
    }
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

/**
 * This method will warm up the system and will run measurement tests.
 */
void Client::run_tests(bool incremental_msg_size) {

    /* warm up until latency converges */
//    warm_up();
    for (unsigned int stream_idx = 0; stream_idx < this->num_of_streams; stream_idx++) { //todo
        if (DEBUG) { printf("DEBUG: %s\n", "run tests warm up loop"); }
        warm_up(&(this->server_sockets[stream_idx]));
    }

    //TODO check ssize_t limit
    ssize_t max_packet_size = ONE_BYTE;

    if (incremental_msg_size) {
        max_packet_size = ONE_MEGABYTE_IN_BYTES;
    }

    /* Measure throughput and latency , for exponential series of message sizes */
    for (ssize_t packet_size = ONE_BYTE; packet_size <= max_packet_size; packet_size = packet_size << 1u) {
        /* Init the packet message to send*/
        char msg[packet_size];


        /* Preforming tests and printing results */
        measure_throughput(msg, packet_size);
        calculate_packet_rate(packet_size);
        for (unsigned int stream_idx = 0; stream_idx < num_of_streams; stream_idx++) { //todo
            measure_latency(&this->server_sockets[stream_idx], msg, packet_size);
        }
//        measure_latency(msg, packet_size);
        print_results(packet_size);
    }
}

void part1(const char * serverIP) {

    /* Create client object and connect to given server-ip and run tests */
    Client client = Client(serverIP, 1);
    if (DEBUG) { printf("DEBUG: %s\n", "client object has been created"); }

    client.run_tests(false);

    /* Close client and disconnect from server */
    client.kill_client();
}

void part3(const char * serverIP, unsigned int num_of_streams, bool incremental) {
    /* Create client object and connect to given server-ip and run tests */
    Client client = Client(serverIP, num_of_streams);

    client.run_tests(incremental);

    /* Close client and disconnect from server */
    client.kill_client();
}


int main(int argc, char const *argv[]) {    //todo edit
    /* Create client object and connect to given server-ip and run tests */
//    Client client = Client(argv[1]);


    part3(argv[1], 2, false);

    return EXIT_SUCCESS;
}

