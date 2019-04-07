#include "shared.h"


class Server {
    int welcomeSocket;
    int clientfd;
    char readBuf[WARMPUP_PACKET_SIZE + 1] = "0";


public:
    //// C-tor
    Server();
    void echo();
    void warmup_echo_back(bool * keepEcho);
    void print_error(const std::string& function_name, int error_number);
    //// server actions
    void killServer();
private:

};

Server::Server() {

    struct sockaddr_in serverAddress;

    bzero(&serverAddress, sizeof(struct sockaddr_in));

    welcomeSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (welcomeSocket < 0) {
        print_error("socket() error", errno);
    }
    int enable = 1;
    if (setsockopt(welcomeSocket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0){
        print_error("setsockopt", errno);
    }

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(PORT_NUMBER);

    int retVal = bind(welcomeSocket, (struct sockaddr*) &serverAddress, sizeof(serverAddress));
    if (retVal < 0) {
        print_error("bind", errno);
    }

    retVal = listen(welcomeSocket, MAX_INCOMING_QUEUE);

    if (retVal < 0) { print_error("listen", errno); }

    // accepting socket - server will enter BLOCKING STATE until client connects.
    retVal = accept(welcomeSocket, nullptr, nullptr);

    if (retVal < 0) {
        print_error("accept", errno);
    }

    this->clientfd = retVal;

    bzero(this->readBuf, WARMPUP_PACKET_SIZE + 1);
}

void Server::killServer() {
    // close client socket
    int retVal = close(this->clientfd);
    if (retVal < 0) {
        print_error("close() failed.", errno);
    }

    // close welcome socket
    retVal = close(this->welcomeSocket);
    if (retVal < 0) {
        print_error("close() failed.", errno);
    }

}

void Server::warmup_echo_back(bool * keepEcho) {
    int received = 0;
    ssize_t retVal = recv(this->clientfd, this->readBuf, (size_t) WARMPUP_PACKET_SIZE, 0);
    if (retVal < 0) {
        print_error("recv() failed", errno);
    }
    if (retVal == 0) {
        *keepEcho = false;
        return;
    }
    received++;
    while (retVal > 0) {
        retVal = send(this->clientfd, this->readBuf, (size_t) WARMPUP_PACKET_SIZE, 0);
        if (retVal != WARMPUP_PACKET_SIZE) {
            print_error("send() failed", errno);
        }
        if (received == PACKETS_PER_CYCLE) {
            if (DEBUG) { std::cout << "received " << PACKETS_PER_CYCLE << " msgs" << std::endl; }

            return;
        }
        retVal = recv(this->clientfd, this->readBuf, (size_t) WARMPUP_PACKET_SIZE, 0);
        if (retVal < 0) {
            print_error("recv() failed", errno);
        }
        received++;

    }

    if (DEBUG) { std::cout << "received total of " << received << std::endl; }

}

void Server::echo() {
    bool keepEcho = true;
    int echoCounter = 0;    // TODO DELETE DEBUG
    int test = 1;

    while (keepEcho) {
        // todo do we need to clean the
        ssize_t retVal = recv(this->clientfd, this->readBuf, (size_t) WARMPUP_PACKET_SIZE, 0);
        if (DEBUG) { std::cout << "msg received size: " << retVal<< std::endl; }
        if (retVal < 0) {
            print_error("recv() failed", errno);
        }
        if (retVal == 0) {
            // means we didn't read anything from client - we assume client closed the socket.
            // from man recv():
            // When  a  stream socket peer has performed an orderly shutdown,
            // the return value will be 0 (the traditional "end-of-file" return).

            keepEcho = false;
            if (DEBUG) { std::cout << "received total of " << echoCounter << std::endl; }
            return;
        }
        echoCounter++;    // TODO DELETE DEBUG
        if (test != this->readBuf[0]) {
            if (DEBUG) { std::cout << "~~~~~~~~~~ERRRROR " << std::endl; }
        }
        // TODO DELETE ME
        if (this->readBuf[0] == 0) {
            test = 1;
        } else {
            test = 0;
        }

        retVal = send(this->clientfd, this->readBuf, (size_t) retVal, 0);
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
    server.echo();
    server.killServer();

    return EXIT_SUCCESS;
}