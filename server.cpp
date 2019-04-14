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
    // setup sockets and structs
    struct sockaddr_in serverAddress;

    bzero(&serverAddress, sizeof(struct sockaddr_in));

    welcome_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (welcome_socket < 0) {
        print_error("socket() error", errno);
    }
    int enable = 1;
    if (setsockopt(welcome_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0){
        print_error("setsockopt", errno);
    }

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(PORT_NUMBER);

    int retVal = bind(welcome_socket, (struct sockaddr*) &serverAddress, sizeof(serverAddress));
    if (retVal < 0) {
        print_error("bind", errno);
    }

    retVal = listen(welcome_socket, MAX_INCOMING_QUEUE);

    if (retVal < 0) { print_error("listen", errno); }

    // accepting socket - server will enter BLOCKING STATE until client connects.
    retVal = accept(welcome_socket, nullptr, nullptr);

    if (retVal < 0) {
        print_error("accept", errno);
    }

    this->client_fd = retVal;

    bzero(this->read_buffer, WARMPUP_PACKET_SIZE + 1);
}

/**
 * close the open sockets.
 */
void Server::killServer() {
    // close client socket
    int retVal = close(this->client_fd);
    if (retVal < 0) {
        print_error("close() failed.", errno);
    }

    // close welcome socket
    retVal = close(this->welcome_socket);
    if (retVal < 0) {
        print_error("close() failed.", errno);
    }

}

/**
 * This method will echo back the client with the message it sent.
 */
void Server::echo() {
    int echoCounter = 0;    // TODO DELETE DEBUG

    while (true) {
        // keep loop until client closed the socket.
        ssize_t retVal = recv(this->client_fd, this->read_buffer, (size_t) WARMPUP_PACKET_SIZE, 0);
        if (DEBUG) { std::cout << "msg received size: " << retVal<< std::endl; }
        if (retVal < 0) {
            print_error("recv() failed", errno);
        }
        if (retVal == 0) {
            // Means we didn't read anything from client - we assume client closed the socket.
            // from man recv():
            // When a stream socket peer has performed an orderly shutdown,
            // the return value will be 0 (the traditional "end-of-file" return).
            if (DEBUG) { std::cout << "received total of " << echoCounter << " packets" << std::endl; }
            return;
        }

        echoCounter++;    // TODO DELETE DEBUG

        retVal = send(this->client_fd, this->read_buffer, (size_t) retVal, 0);
        if (retVal < 0) {
            print_error("send() failed", errno);
        }
        if (DEBUG) { std::cout << "msg sent size: " << retVal << std::endl; }
    }

}

void Server::print_error(const std::string& function_name, int error_number) {
    printf("ERROR: %s %d.\n", function_name.c_str(), error_number);
    exit(EXIT_FAILURE);
}

int main() {
    // setup server, echo back client's messages when done, kill server.
    Server server =  Server();
    server.echo();  // TODO check if need to call echo again.
    server.killServer();

    return EXIT_SUCCESS;
}