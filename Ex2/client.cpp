#include <fstream>
#include "shared.h"


using fp_milliseconds = std::chrono::duration<double, std::chrono::milliseconds::period>;
using fp_seconds = std::chrono::duration<double, std::chrono::seconds::period>;


/**
 * This struct would contain info regarding specific socket
 */
struct TCPSocket {
    int socked_fd = -1;
    char * read_buffer;
    double latency_result = 0.0;
};


/**
 * Constructor of Client class.
 */
class Client {
    TCPSocket server_sockets[MAX_PARALLEL_STREAMS];
    unsigned int num_of_streams = MIN_PARALLEL_STREAMS;
    unsigned int num_of_active_streams = MIN_PARALLEL_STREAMS;
    unsigned int num_of_threads = 1;

public:
    //// C-tor
    explicit Client(const char * serverIP, bool multiStreams);
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
Client::Client(const char * serverIP, bool multiStreams) {

    if (num_of_streams > MAX_PARALLEL_STREAMS) {
        print_error("num of streams is over the limit", 1);
    }

    if (multiStreams) {
        this->num_of_streams = MAX_PARALLEL_STREAMS;
    } else {
        this->num_of_streams = MIN_PARALLEL_STREAMS;
    }

    /* setup sockets and structs */
    struct sockaddr_in server_address;

    if (SAVE_RESULTS_TO_CSV) {
        /* open csv file */
        this->results_file.open("tcp1.csv", std::ofstream::app);
        //todo fix this line
        this->results_file << "Message size,#sockets,#threads,Total latency,Total throughput,Total packet rate,";
        this->results_file << std::endl;
    }

    /* create sockets */
    for (unsigned int stream_idx = 0; stream_idx < num_of_streams; stream_idx++) {
        printf("ste %u\n", stream_idx);
        bool socket_creation_failed = false;

        int current_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (current_socket < 0) {
            print_error("socket() error", errno);
            socket_creation_failed = true;
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
    /* Set chrono clocks*/
    steady_clock::time_point cycle_start_time, cycle_end_time;
    steady_clock::duration cycleTime;
    this->max_throughput_result = 0.0;

    /* init calculations */
    auto cycle_bytes_transferred = this->num_of_active_streams * 2 * RTT_PACKETS_PER_CYCLE * packet_size;
    auto bits_transferred_per_cycle = cycle_bytes_transferred * BYTES_TO_BITS;

    /* Init the packet message to send*/
    memset(msg, 1, packet_size);

    /* Measure throughput for pre defined # of cycle */
    for (int cycle_index = 0; cycle_index < RTT_NUM_OF_CYCLES; cycle_index++) {
        cycle_start_time = steady_clock::now();

        /* Sending continuously pre defined # of packets */
        for (int packet_index = 0; packet_index < RTT_PACKETS_PER_CYCLE; packet_index++) {

            for (unsigned int stream_idx = 0; stream_idx < num_of_active_streams; stream_idx++) {   //todo EXP CODE
                /* Send packet and verify the #bytes sent equal to #bytes requested to sent. */
                ssize_t ret_value = send(this->server_sockets[stream_idx].socked_fd, msg,
                                         packet_size, 0);
                if (ret_value != packet_size) { print_error("send() failed", errno); }
            }

            for (unsigned int stream_idx = 0; stream_idx < num_of_active_streams; stream_idx++) {
                /* Receive packet and verify the #bytes sent. */
                int ret_value = recv(this->server_sockets[stream_idx].socked_fd, this->server_sockets[stream_idx].read_buffer, packet_size, 0);
                if (ret_value < 0) { print_error("recv() failed", errno); }
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
    for (unsigned int stream_idx = 0; stream_idx < this->num_of_active_streams; stream_idx++) {
        this->latency_result += this->server_sockets[stream_idx].latency_result;
    }

    this->latency_result /= this->num_of_active_streams;

    if (SAVE_RESULTS_TO_CSV) {
        this->results_file << packet_size << " " << packet_unit << ",";
        this->results_file << this->num_of_active_streams << ",";
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

    while (num_of_active_streams <= num_of_streams) {
        /* Measure throughput and latency , for exponential series of message sizes */
        for (ssize_t packet_size = ONE_BYTE; packet_size <= max_packet_size; packet_size = packet_size << 1u) {

            /* Init the packet message to send*/
            char msg[packet_size];

            /* Preforming tests and printing results */
            measure_throughput(msg, packet_size);
            calculate_packet_rate(packet_size);

            for (unsigned int stream_idx = 0; stream_idx < num_of_active_streams; stream_idx++) {
                measure_latency(&this->server_sockets[stream_idx], msg, packet_size);
            }

            print_results(packet_size);
        }
        num_of_active_streams++;
    }
}


int main(int argc, char const *argv[]) {
    if(argc != 2) {
        printf("Usage: client <IPv4 address>\n");
        exit(EXIT_FAILURE);
    }

    /* Create client object and connect to given server-ip and incremental streams. */
    Client client = Client(argv[1], true);

    /* run tests with incremental message size */
    client.run_tests(true);

    /* Close client and disconnect from server */
    client.kill_client();
    return EXIT_SUCCESS;
}

