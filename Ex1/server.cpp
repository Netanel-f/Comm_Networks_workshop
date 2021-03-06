#include "shared.h"


class Server {
    int welcome_socket;
    int client_fd;
    char read_buffer[WARMPUP_PACKET_SIZE + 1] = "0";

public:
    //// C-tor
    Server();
    //// server actions
    void echo();
    void killServer();

private:
    static void print_error(const std::string& function_name, int error_number);
};


/**
 * Server constructor. Setup welcome socket and waiting until client to connect.
 */
Server::Server() {
    /* setup sockets and structs */
    struct sockaddr_in server_address;

    bzero(&server_address, sizeof(struct sockaddr_in));

    welcome_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (welcome_socket < 0) { print_error("socket() error", errno); }

    int enable = 1;
    if (setsockopt(welcome_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0){
        print_error("setsockopt", errno);
    }

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(PORT_NUMBER);

    int ret_value = bind(welcome_socket, (struct sockaddr*) &server_address, sizeof(server_address));
    if (ret_value < 0) { print_error("bind", errno); }

    ret_value = listen(welcome_socket, MAX_INCOMING_QUEUE);
    if (ret_value < 0) { print_error("listen", errno); }

    // accepting socket - server will enter BLOCKING STATE until client connects.
    ret_value = accept(welcome_socket, nullptr, nullptr);

    if (ret_value < 0) { print_error("accept", errno); }

    this->client_fd = ret_value;

    bzero(this->read_buffer, WARMPUP_PACKET_SIZE + 1);
}

/**
 * close the open sockets.
 */
void Server::killServer() {
    // close client socket
    int ret_value = close(this->client_fd);
    if (ret_value < 0) { print_error("close() failed.", errno); }

    // close welcome socket
    ret_value = close(this->welcome_socket);
    if (ret_value < 0) { print_error("close() failed.", errno); }
}

/**
 * This method will echo back the client with the message it sent.
 */
void Server::echo() {
    while (true) {
        /* keep loop until client closed the socket. */
        ssize_t ret_value = recv(this->client_fd, this->read_buffer, (size_t) WARMPUP_PACKET_SIZE, 0);
        if (ret_value < 0) { print_error("recv() failed", errno); }

        /* return value == 0:
         * Means we didn't read anything from client - we assume client closed the socket.
         * Quote from recv() manual:
         * When a stream socket peer has performed an orderly shutdown,
         * the return value will be 0 (the traditional "end-of-file" return). */
        if (ret_value == 0) { return; }

        ret_value = send(this->client_fd, this->read_buffer, (size_t) ret_value, 0);
        if (ret_value < 0) { print_error("send() failed", errno); }
    }
}

/**
 * This method will print the function that raised the given error number.
 * @param function_name function that caused error
 * @param error_number errno that been raised.
 */
void Server::print_error(const std::string& function_name, int error_number) {
    printf("ERROR: %s %d.\n", function_name.c_str(), error_number);
    exit(EXIT_FAILURE);
}

int main() {

    /* Create server object and wait for client to connect */
    Server server =  Server();

    /* Echo back client's messages */
    server.echo();

    /* Close open sockets and close server */
    server.killServer();
    return EXIT_SUCCESS;
}