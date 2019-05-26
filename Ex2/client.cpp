#include <fstream>
#include "shared.h"


using fp_milliseconds = std::chrono::duration<double, std::chrono::milliseconds::period>;
using fp_seconds = std::chrono::duration<double, std::chrono::seconds::period>;

#define SAVE_RESULTS_TO_CSV false //todo set true when submit


/**
 * This struct would contain info regarding specific socket
 */
struct TCPSocket {
    int socked_fd = -1;
    char * read_buffer;
    double latency_result = 0.0;
    int packet_sent = 0;
    int packet_received = 0;
};


/**
 * Constructor of Client class.
 */
class Client {
    TCPSocket server_sockets[MAX_PARALLEL_STREAMS];
    unsigned int num_of_streams = 1;
    unsigned int num_of_threads = 1;
    int max_fd = 0;

public:
    //// C-tor
    explicit Client(const char * serverIP, unsigned int num_of_streams);
    //// client actions
    void run_tests(bool incremental_msg_size);
    void kill_client();

private:
    static void print_error(const std::string& function_name, int error_number);
    void warm_up(TCPSocket * tcpSocket);
    void measure_throughput(char * msg, ssize_t packet_size);
    void calculate_packet_rate(ssize_t packet_size);
    void measure_latency(TCPSocket * tcpSocket, char * msg, ssize_t packet_size);
    void print_results(ssize_t packet_size);
    //// Keeping private variables to store results.
    double max_throughput_result = 0.0;
    double packet_rate_result = 0.0;
    double latency_result = 0.0;
    std::ofstream results_file;
};


/**
 * Thie method will create Client Object and will connect the client to server
 * @param serverIP - the server destination ipv4 format.
 */
Client::Client(const char * serverIP, unsigned int num_of_streams) {

    if (num_of_streams > MAX_PARALLEL_STREAMS) {
        print_error("num of streams is over the limit", 1);
    }
    this->num_of_streams = num_of_streams;
    /* setup sockets and structs */
    struct sockaddr_in server_address;

    if (SAVE_RESULTS_TO_CSV) {
        /* open csv file */
        this->results_file.open("tcp1.csv", std::ofstream::app);
        //todo fix this line
        this->results_file << "Message size,#sockets,#threads,Total latency,Total throughput,Total packet rate,\n";
    }

    for (unsigned int stream_idx = 0; stream_idx < num_of_streams; stream_idx++) {
        printf("ste %u\n", stream_idx);
        bool socket_creation_failed = false;

        int current_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (current_socket < 0) {
            print_error("socket() error", errno);
            socket_creation_failed = true;
        }

        /* code for SELECT */
        if (current_socket > this->max_fd) {
            this->max_fd = current_socket;
        }

        server_sockets[stream_idx].socked_fd = current_socket;

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
        this->server_sockets[stream_idx].read_buffer = new char[MAX_PACKET_SIZE_BYTES + 1];
        bzero(this->server_sockets[stream_idx].read_buffer, MAX_PACKET_SIZE_BYTES + 1);  // clear read_buffer
    }
}


/**
 * Warm up the TCP/IP protocol to measure optimal results.
 * @param tcpSocket socket to warm
 */
void Client::warm_up(TCPSocket * tcpSocket) {

    char msg[WARMUP_PACKET_SIZE];
    measure_latency(tcpSocket, msg, WARMUP_PACKET_SIZE);

    double rtt = tcpSocket->latency_result;

    /* Set chrono clocks*/
    steady_clock::time_point warm_up_start_time = steady_clock::now();

    while (true) {
        measure_latency(tcpSocket, msg, WARMUP_PACKET_SIZE);

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
    //todo EXPERIMENTAL CODE
    /* Set chrono clocks*/
    steady_clock::time_point cycle_start_time, cycle_end_time;
    steady_clock::duration cycleTime;
    this->max_throughput_result = 0.0;

    /* init calculations */
    auto cycle_bytes_transferred = this->num_of_streams * 2 * RTT_PACKETS_PER_CYCLE * packet_size;
    auto bits_transferred_per_cycle = cycle_bytes_transferred * BYTES_TO_BITS;

    /* Init the packet message to send*/
    memset(msg, 1, packet_size);

    /* Measure throughput for pre defined # of cycle */
    for (int cycle_index = 0; cycle_index < RTT_NUM_OF_CYCLES; cycle_index++) {
        if (DEBUG) { printf("**cycle_index: %d\n", cycle_index); }
        //todo alpha
        fd_set r_streams;
        fd_set w_streams;
        FD_ZERO(&r_streams);
        FD_ZERO(&w_streams);
        for (unsigned int stream_idx = 0; stream_idx < num_of_streams; stream_idx++) {
            FD_SET(this->server_sockets[stream_idx].socked_fd, &w_streams);
        }
        int num_ready_incoming_fds;
        int done_cycle = 0;

        cycle_start_time = steady_clock::now();

        while (done_cycle < num_of_streams) {
            if (DEBUG) { printf("**done cycle = %d\n", done_cycle); }
            num_ready_incoming_fds = select((this->max_fd + 1), &r_streams, &w_streams, nullptr, nullptr);
            if (num_ready_incoming_fds == -1) {
                // select error
                print_error("select", errno);

            } else if (num_ready_incoming_fds == 0) {
                if (DEBUG) { printf("**#incoming fds = 0"); }
                continue;
            }

            for (unsigned int stream_idx = 0; stream_idx < num_of_streams; stream_idx++) {
                int current_socket = this->server_sockets[stream_idx].socked_fd;
                if (FD_ISSET(current_socket, &w_streams)) {
                    ssize_t ret_value = send(this->server_sockets[stream_idx].socked_fd, msg, packet_size, 0);
                    if (ret_value != packet_size) { print_error("send() failed", errno); }
                    FD_CLR(current_socket, &w_streams);
                    FD_SET(current_socket, &r_streams);
                    this->server_sockets[stream_idx].packet_sent++;

                } else if (FD_ISSET(this->server_sockets[stream_idx].socked_fd, &r_streams)) {
                    ssize_t ret_value = recv(this->server_sockets[stream_idx].socked_fd, this->server_sockets[stream_idx].read_buffer, packet_size, 0);
                    if (ret_value < 0) { print_error("recv() failed", errno); }
                    FD_CLR(current_socket, &r_streams);
                    this->server_sockets[stream_idx].packet_received++;
                    if (this->server_sockets[stream_idx].packet_received != RTT_PACKETS_PER_CYCLE) {
                        FD_SET(current_socket, &w_streams);
                    } else {
                        this->server_sockets[stream_idx].packet_sent= 0;
                        this->server_sockets[stream_idx].packet_received = 0;
                        done_cycle++;
                    }
                }
            }
        }

        cycle_end_time  = steady_clock::now();

        auto cycle_time_seconds = fp_seconds(cycle_end_time - cycle_start_time);

        auto cycle_throughput = bits_transferred_per_cycle / cycle_time_seconds.count();

        if (cycle_throughput > this->max_throughput_result) {
            this->max_throughput_result = cycle_throughput;
        }
    }
}


///**
// * This method will measure throughput of TCP socket using message with certain size to send.
// * @param msg pointer to char[packet_size]
// * @param packet_size the size of message
// */
//void Client::measure_throughput(char * msg, ssize_t packet_size) {
//    /* Set chrono clocks*/
//    steady_clock::time_point cycle_start_time, cycle_end_time;
//    steady_clock::duration cycleTime;
//    this->max_throughput_result = 0.0;
//
//    /* init calculations */
//    auto cycle_bytes_transferred = this->num_of_streams * 2 * RTT_PACKETS_PER_CYCLE * packet_size;
//    auto bits_transferred_per_cycle = cycle_bytes_transferred * BYTES_TO_BITS;
//
//    /* Init the packet message to send*/
//    memset(msg, 1, packet_size);
//
//    /* Measure throughput for pre defined # of cycle */
//    for (int cycle_index = 0; cycle_index < RTT_NUM_OF_CYCLES; cycle_index++) {
//        cycle_start_time = steady_clock::now();
//
//        /* Sending continuously pre defined # of packets */
//        for (int packet_index = 0; packet_index < RTT_PACKETS_PER_CYCLE; packet_index++) {
//
//            for (unsigned int stream_idx = 0; stream_idx < num_of_streams; stream_idx++) {   //todo
//                /* Send packet and verify the #bytes sent equal to #bytes requested to sent. */
//                ssize_t ret_value = send(this->server_sockets[stream_idx].socked_fd, msg, packet_size, 0);
//                if (ret_value != packet_size) { print_error("send() failed", errno); }
//
//                /* Receive packet and verify the #bytes sent. */
//                ret_value = recv(this->server_sockets[stream_idx].socked_fd, this->server_sockets[stream_idx].read_buffer, packet_size, 0);
//                if (ret_value < 0) { print_error("recv() failed", errno); }
//            }
//        }
//
//        cycle_end_time  = steady_clock::now();
//
//        auto cycle_time_seconds = fp_seconds(cycle_end_time - cycle_start_time);
//
//        auto cycle_throughput = bits_transferred_per_cycle / cycle_time_seconds.count();
//
//        if (cycle_throughput > this->max_throughput_result) {
//            this->max_throughput_result = cycle_throughput;
//        }
//    }
//}


/**
  * This method will calculate the packet rate, based on previously throughput measured.
  * @param packet_size packet size to calculate by.
  */
void Client::calculate_packet_rate(ssize_t packet_size) {
    this->packet_rate_result =  (this->max_throughput_result) / packet_size;
}

/**
 * This method will measure latency of TCP socket using message with certain size to send.
 * @param tcpSocket socket to measure
 * @param msg pointer to char[packet_size]
 * @param packet_size the size of message
 */
void Client::measure_latency(TCPSocket * tcpSocket, char * msg, ssize_t packet_size) {
    /* Reset latency_result variable */

    /* Set chrono clocks*/
    steady_clock::time_point start_time, end_time;

    /* Init the packet message to send*/
    memset(msg, 1, packet_size);

    /* Take time*/
    start_time = steady_clock::now();

    /* Send 1 packet with (size_t) packet_size */
    ssize_t ret_value = send(tcpSocket->socked_fd, msg, packet_size, 0);
    if (ret_value != packet_size) { print_error("send() failed", errno); }

    /* Receive 1 packet with (size_t) packet_size */
    ret_value = recv(tcpSocket->socked_fd, tcpSocket->read_buffer, packet_size, 0);
    if (ret_value < 0) { print_error("recv() failed", errno); }

    end_time = steady_clock::now();

    auto rtt = fp_milliseconds(steady_clock::duration(end_time - start_time));
    auto latency = rtt.count() / 2;
    tcpSocket->latency_result = latency;
}


/**
 * This method will print results to screen as saved in client's object.
 * @param packet_size the packet size results.
 */
void Client::print_results(ssize_t packet_size) {
    /* Adjusting packet size units */
    std::string packet_unit;
    if (packet_size >= MEGABYTE_IN_BYTES) {
        packet_size /= MEGABYTE_IN_BYTES;
        packet_unit = "MB";

    } else if (packet_size >= MEGABYTE_IN_KILOBYTES) {
        packet_size /= MEGABYTE_IN_KILOBYTES;
        packet_unit = "KB";

    } else {
        packet_unit = "Bytes";
    }


    /* Adjusting maximal throughput measured result*/
    std::string rate_unit;
    if (max_throughput_result >= GIGABIT_IN_BITS) {
        max_throughput_result /= GIGABIT_IN_BITS;
        rate_unit = "Gbps";

    } else if (max_throughput_result >= MEGABIT_IN_BITS) {
        max_throughput_result /= MEGABIT_IN_BITS;
        rate_unit = "Mbps";

    } else if (max_throughput_result >= KILOBIT_IN_BITS) {
        max_throughput_result /= KILOBIT_IN_BITS;
        rate_unit = "Kbps";

    } else {
        rate_unit = "bps";
    }

    // calc total latency:
    for (unsigned int stream_idx = 0; stream_idx < this->num_of_streams; stream_idx++) {
        this->latency_result += this->server_sockets[stream_idx].latency_result;
    }

    this->latency_result /= this->num_of_streams;

    if (SAVE_RESULTS_TO_CSV) {
        this->results_file << packet_size << " " << packet_unit << ",";
        this->results_file << this->num_of_streams << ",";
        this->results_file << this->num_of_threads << ",";
        this->results_file << this->latency_result << " " << "milliseconds" << ",";
        this->results_file << this->max_throughput_result << " " << rate_unit << ",";
        this->results_file << this->packet_rate_result << " " << "packets/second" << ",";
        this->results_file << std::endl;
    } else {
        printf(RESULTS_FORMAT, packet_size, packet_unit.c_str(), this->num_of_streams,
                this->num_of_threads, this->latency_result, "milliseconds",
                max_throughput_result, rate_unit.c_str(), packet_rate_result, "packets/second");
    }
}


/**
 * close the open socket towards server.
 */
void Client::kill_client() {
    if (SAVE_RESULTS_TO_CSV) {
        this->results_file.flush();
        this->results_file.close();
    }

    for (unsigned int stream_idx = 0; stream_idx < this->num_of_streams; stream_idx++) {
        delete(this->server_sockets[stream_idx].read_buffer);
        shutdown(this->server_sockets[stream_idx].socked_fd, SHUT_RDWR);
        close(this->server_sockets[stream_idx].socked_fd);
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

    for (unsigned int stream_idx = 0; stream_idx < this->num_of_streams; stream_idx++) { //todo
        /* warm up until latency converges */
        warm_up(&(this->server_sockets[stream_idx]));
    }

    ssize_t max_packet_size = ONE_BYTE;

    if (incremental_msg_size) {
        max_packet_size = MEGABYTE_IN_BYTES;
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


//todo delete
//        if (packet_size == ONE_BYTE) { printf(RESULTS_HEADER); }
        print_results(packet_size);
    }
}

void part1(const char * serverIP) {

    /* Create client object and connect to given server-ip and run tests */
    Client client = Client(serverIP, 1);

    client.run_tests(false);

    /* Close client and disconnect from server */
    client.kill_client();
}

void part3(const char * serverIP, bool multiStreams, bool incMsgSize) {
    printf("\n**testing multi-streams: %s, incremental size: %s**\n", (multiStreams ? "true" : "false"), (incMsgSize ? "true" : "false"));
    unsigned int max_streams = 1;
    if (multiStreams) { max_streams = MAX_PARALLEL_STREAMS; }

    for (unsigned int num_of_streams = 1; num_of_streams <= max_streams; num_of_streams++) {
        /* Create client object and connect to given server-ip and run tests */
        Client client = Client(serverIP, num_of_streams);

        client.run_tests(incMsgSize);

        /* Close client and disconnect from server */
        client.kill_client();
    }
}


int main(int argc, char const *argv[]) {
    /* Create client object and connect to given server-ip and run tests */
//    Client client = Client(argv[1]);

//todo set on when submit
//    if(argc != 2) {
//        printf("Usage: client <IPv4 address>\n");
//        exit(EXIT_FAILURE);
//    }

//    part3(argv[1], false, false);
//todo delete DEBUG ONLY
    if (argc == 3) {
        if (strcmp(argv[2], "1") == 0) {
            part3(argv[1], false, false);
        } else if (strcmp(argv[2], "2") == 0) {
            part3(argv[1], false, true);
        } else if (strcmp(argv[2], "3") == 0) {
            part3(argv[1], true, false);
        } else if (strcmp(argv[2], "4") == 0) {
            part3(argv[1], true, true);
        }
    } else {
        part3(argv[1], false, false);
        part3(argv[1], false, true);
        part3(argv[1], true, false);
        part3(argv[1], true, true);
    }

    return EXIT_SUCCESS;
}

