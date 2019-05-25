#include <algorithm>
#include "shared.h"


class Server {
    int welcome_socket;
    int client_fd;  //TODO remove

    char read_buffer[WARMPUP_PACKET_SIZE + 1] = "0";
    //char write_buffer[WARMPUP_PACKET_SIZE + 1] = "0"; // todo remove

    // clients sockets
    std::map<std::string, int> clients_sockets;
    fd_set clients_fds;
    fd_set read_fds;

public:
    //// C-tor
    Server();
    //// server actions
    void selectPhase();
    void echoClient(int ec_client_fd); //todo change to client_fd
    void echo();
    void killServer();

private:
    int getMaxFd();
    static void print_error(const std::string& function_name, int error_number);
};


/**
 * Server constructor. Setup welcome socket and waiting until client to connect.
 */
Server::Server() {
    /* setup sockets and structs and listening to welcome socket */
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

//    // TODO remove
//    // accepting socket - server will enter BLOCKING STATE until client connects.
//    ret_value = accept(welcome_socket, nullptr, nullptr);
//
//    if (ret_value < 0) { print_error("accept", errno); }
//
//    this->client_fd = ret_value;

    bzero(this->read_buffer, WARMPUP_PACKET_SIZE + 1);
//    bzero(this->write_buffer, WARMPUP_PACKET_SIZE + 1);
}


int Server::getMaxFd() {
    int maxfd = this->welcome_socket;
    for (auto client : this->clients_sockets) {
        if (client.second > maxfd) { maxfd = client.second; }
    }
    return maxfd;
}


void Server::selectPhase() {
    if (DEBUG) { printf("DEBUG: %s\n", "select phase"); }
    /* initializing arguments for select() */
    FD_ZERO(&clients_fds);
    FD_SET(welcome_socket, &clients_fds);

    bool keep_loop_select = true;
    int num_ready_incoming_fds = 0;

    while (keep_loop_select) {
        int max_fd = getMaxFd();
        if (DEBUG) { printf("DEBUG: %s\n", "select loop"); }
        read_fds = clients_fds;
        num_ready_incoming_fds = select((max_fd + 1), &read_fds, nullptr, nullptr, nullptr);

        if (num_ready_incoming_fds == -1) {
            // select error
            print_error("select", errno);

        } else if (num_ready_incoming_fds == 0) {
            // None of the incoming fds is ready
            continue;
        }
        // at least one incoming fd is ready

        /* new client arrived welcome */
        if (FD_ISSET(welcome_socket, &read_fds)) {
            // accepting socket - server will enter BLOCKING STATE until client connects.
            int new_client_socket = accept(welcome_socket, nullptr, nullptr);
            if (new_client_socket < 0) {
                print_error("accept", errno);
            } else {
                FD_SET(new_client_socket, &clients_fds);
                clients_sockets.emplace(std::to_string(new_client_socket), new_client_socket);
            }
        }

        /* checking other sockets */
        if (!this->clients_sockets.empty()) {
            for (auto client: this->clients_sockets) {
                if (FD_ISSET(client.second, &read_fds)) {
                    if (DEBUG) { printf("DEBUG: %s %d\n", "echo client", client.second); }
                    echoClient(client.second);
                }
            }
        }
    }

}


void Server::echoClient(int ec_client_fd) {
    ssize_t ret_value = recv(ec_client_fd, this->read_buffer, (size_t) WARMPUP_PACKET_SIZE, 0);
    if (ret_value < 0) { print_error("recv() failed", errno); }

    /* return value == 0:
     * Means we didn't read anything from client - we assume client closed the socket.
     * Quote from recv() manual:
     * When a stream socket peer has performed an orderly shutdown,
     * the return value will be 0 (the traditional "end-of-file" return). */
    if (ret_value == 0) {
        // close client socket
        FD_CLR(ec_client_fd, &this->clients_fds);
//        ret_value = close(ec_client_fd);
        ret_value = shutdown(ec_client_fd, SHUT_RDWR);
        if (ret_value < 0) { print_error("close() failed.", errno); }

        this->clients_sockets.erase(std::to_string(ec_client_fd));
        if (this->clients_sockets.empty()) {
            killServer();
        }
    } else {
        ret_value = send(ec_client_fd, this->read_buffer, (size_t) ret_value, 0);
        if (ret_value < 0) { print_error("send() failed", errno); }
    }

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
 * close the open sockets.
 */
void Server::killServer() {
//    // close client socket
//    int ret_value = close(this->client_fd);
//    if (ret_value < 0) { print_error("close() failed.", errno); }

    // close welcome socket
    int ret_value = close(this->welcome_socket);
    if (ret_value < 0) { print_error("close() failed.", errno); }
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

    /* Create server object and listening to clients on welcome socket */
    Server server =  Server();

    if (DEBUG) { printf("DEBUG: %s\n", "server has been created."); }
    /* Server select loop phase for handling (new/ old/ leaving) clients */
    server.selectPhase();

    /* Echo back client's messages */
//    server.echo();

    /* Close open sockets and close server */
    server.killServer();
    // TODO time out if no clients arrived on time
    return EXIT_SUCCESS;
}