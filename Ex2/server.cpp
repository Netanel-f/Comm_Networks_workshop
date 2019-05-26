#include <algorithm>
#include "shared.h"


class Server {
    int welcome_socket;
    char * read_buffer;
//    char read_buffer[MAX_PACKET_SIZE_BYTES + 1] = "0";
    bool keep_loop_select = true;
//    bool server_can_shutdown = false;
//    steady_clock::time_point last_socket_activity_timestamp;

    // clients sockets
    std::map<std::string, int> clients_sockets;
    fd_set clients_fds;
    fd_set read_fds;

public:
    //// C-tor
    Server();
    //// server actions
    void selectPhase();
    void echoClient(int client_fd);
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
    //todo del before submit
    if (setsockopt(welcome_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0){
//    if (setsockopt(welcome_socket, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0){
        print_error("setsockopt", errno);
    }

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(PORT_NUMBER);

    int ret_value = bind(welcome_socket, (struct sockaddr*) &server_address, sizeof(server_address));
    if (ret_value < 0) { print_error("bind", errno); }

    ret_value = listen(welcome_socket, MAX_INCOMING_QUEUE);
    if (ret_value < 0) { print_error("listen", errno); }

    this->read_buffer = new char[MAX_PACKET_SIZE_BYTES + 1];
    bzero(this->read_buffer, MAX_PACKET_SIZE_BYTES + 1);
}


int Server::getMaxFd() {
    int maxfd = this->welcome_socket;
    for (auto client : this->clients_sockets) {
        if (client.second > maxfd) { maxfd = client.second; }
    }
    return maxfd;
}


void Server::selectPhase() {
    if (DEBUG) { printf("DEBUG: %s welcomesocket=%d\n", "select phase", welcome_socket); }
    /* initializing arguments for select() */
    FD_ZERO(&clients_fds);
    FD_SET(welcome_socket, &clients_fds);

    int num_ready_incoming_fds = 0;

    while (keep_loop_select) {
        //todo debug
//        auto seconds_idle = duration_cast<seconds>(steady_clock::now() - this->last_socket_activity_timestamp).count();
//        if (this->server_can_shutdown && seconds_idle > 10) {
//            if (DEBUG) { printf("seconds %ld\n", seconds_idle); }
//            keep_loop_select = false;
//            break;
//        }

        int max_fd = getMaxFd();
        if (DEBUG) { printf("DEBUG: %s\n", "select loop"); }
        read_fds = clients_fds;
        struct timeval tv_timeout = {10, 0}; // 10 seconds time out
        num_ready_incoming_fds = select((max_fd + 1), &read_fds, nullptr, nullptr, &tv_timeout);

        if (num_ready_incoming_fds == -1) {
            // select error
            print_error("select", errno);

        } else if (num_ready_incoming_fds == 0) {
            if (DEBUG) { printf("**time out\n"); }
            // None of the incoming fds is ready = timeout!
            if (this->clients_sockets.empty()) {
                // no clients kill server
                if (DEBUG) { printf("**no clients!!\n"); }
                keep_loop_select = false;
                break;

            } else {
                continue;
            }
        }

        /* at least one incoming fd is ready */

        if (FD_ISSET(welcome_socket, &read_fds)) {
            /* new client arrived welcome */
            // accepting socket - server will enter BLOCKING STATE until client connects.
            int new_client_socket = accept(welcome_socket, nullptr, nullptr);
            if (new_client_socket < 0) {
                print_error("accept", errno);

            } else {
                FD_SET(new_client_socket, &clients_fds);
                clients_sockets.emplace(std::to_string(new_client_socket), new_client_socket);
//                this->server_can_shutdown = false;
//                this->last_socket_activity_timestamp = steady_clock::now();
            }
        }

        /* checking other sockets */
        if (!this->clients_sockets.empty()) {
            auto temp_client_sockets = this->clients_sockets;
//            for (auto client: this->clients_sockets) {
            for (auto client: temp_client_sockets) {
                if (FD_ISSET(client.second, &read_fds)) {

                    /* Echo back client's messages */
                    echoClient(client.second);
                }
            }
        }
    }

}

/**
 * This method will echo to specific client fd
 * @param client_fd client socket fd to echo.
 */
void Server::echoClient(int client_fd) {
    ssize_t ret_value = recv(client_fd, this->read_buffer, (size_t) MAX_PACKET_SIZE_BYTES, 0);
    if (ret_value < 0) { print_error("recv() failed", errno); }

    /* return value == 0:
     * Means we didn't read anything from client - we assume client closed the socket.
     * Quote from recv() manual:
     * When a stream socket peer has performed an orderly shutdown,
     * the return value will be 0 (the traditional "end-of-file" return). */
    if (ret_value == 0) {
        // close client socket
        FD_CLR(client_fd, &this->clients_fds);

        ret_value = shutdown(client_fd, SHUT_RDWR);
        if (ret_value < 0) { print_error("shutdown() failed.", errno); }

        ret_value = close(client_fd);
        if (ret_value < 0) { print_error("close() failed.", errno); }

        this->clients_sockets.erase(std::to_string(client_fd));
//        this->last_socket_activity_timestamp = steady_clock::now();
        if (DEBUG) { printf("**erasing %d\n", client_fd); }
        if (this->clients_sockets.empty()) {
            if (DEBUG) { printf("**client_sockets empty\n"); }
//            this->server_can_shutdown = true;
//            this->keep_loop_select = false;
//            killServer();
        }

    } else {
        ret_value = send(client_fd, this->read_buffer, (size_t) ret_value, 0);
        if (ret_value < 0) { print_error("send() failed", errno); }
    }

}


/**
 * close the open sockets.
 */
void Server::killServer() {
    if (DEBUG) { printf("**killing server\n"); }
    /* deleted buffer */
    delete(this->read_buffer);

    fflush(stdout); //todo delete

    /* close welcome socket */
    FD_CLR(this->welcome_socket, &this->clients_fds);
    shutdown(this->welcome_socket, SHUT_RDWR);
    close(this->welcome_socket);
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

    if (DEBUG) { printf("**DEBUG: %s\n", "server has been created."); }

    /* Server select loop phase for handling (new/ old/ leaving) clients */
    server.selectPhase();


    /* Close open sockets and close server */
    server.killServer();
    // TODO time out if no clients arrived on time ?
    return EXIT_SUCCESS;
}