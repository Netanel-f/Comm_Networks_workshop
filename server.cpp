#include <stdio.h>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <string.h>

#include "shared.h"


void print_error(const std::string& function_name, int error_number) {
    printf("ERROR: %s %d.\n", function_name.c_str(), error_number);
    exit(EXIT_FAILURE);
}

class Server {
    int welcomeSocket;
    int clientfd;

    char readBuf[WARMPUP_PACKET_SIZE + 1];
//    char writeBuf[WA_MAX_INPUT+1];

//    fd_set clientsfds;
//    fd_set readfds;

public:
    //// C-tor
    Server();

    //// server actions
    void killServer();
    void warmup_echo_back();

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

//    memset(this->readBuf, 0, WARMPUP_PACKET_SIZE+1);
    bzero(this->readBuf, WARMPUP_PACKET_SIZE + 1);
}

void Server::killServer() {
    int retVal = close(this->clientfd);
    std::cout << "close output: " << retVal << std::endl;
    retVal = close(this->welcomeSocket);
    std::cout << "close output: " << retVal << std::endl;

}

void Server::warmup_echo_back() {
    int received = 0;
    ssize_t retVal = recv(this->clientfd, this->readBuf, (size_t) WARMPUP_PACKET_SIZE, 0);
    if (retVal < 0) {
        print_error("recv() failed", errno);
    }
    received++;
    while (retVal > 0) {
        retVal = send(this->clientfd, this->readBuf, (size_t) WARMPUP_PACKET_SIZE, 0);
        if (retVal != WARMPUP_PACKET_SIZE) {
            print_error("send() failed", errno);
        }
        if (received == PACKETS_PER_CYCLE) {
            std::cout << "received " << PACKETS_PER_CYCLE << " msgs" << std::endl;

            return;
        }
        retVal = recv(this->clientfd, this->readBuf, (size_t) WARMPUP_PACKET_SIZE, 0);
        if (retVal < 0) {
            print_error("recv() failed", errno);
        }
        received++;

    }

    std::cout << "received total of " << received << std::endl;

}

int main() {
    std::cout << "Hello, World!" << std::endl;

    Server server =  Server();

    for (int i=0; i<MIN_WARMUP_CYCLES; i++) {
        std::cout << "warmup_echo_back #" << i << std::endl;

        server.warmup_echo_back();
    }


    std::cout << "Bye, World!" << std::endl;
    server.killServer();


    return 0;
}