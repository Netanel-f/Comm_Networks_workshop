#include <stdio.h>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <string.h>

#define MAX_INCOMING_QUEUE 1
#define PORT_NUMBER 54321

void print_error(const std::string& function_name, int error_number) {
    printf("ERROR: %s %d.\n", function_name.c_str(), error_number);
    exit(EXIT_FAILURE);
}

class Server {
    int welcomeSocket;
    int clientfd;

//    char readBuf[WA_MAX_INPUT+1];
//    char writeBuf[WA_MAX_INPUT+1];

//    fd_set clientsfds;
//    fd_set readfds;

public:
    //// C-tor
    Server();

    //// server actions

private:
    void killServer();
};

Server::Server() {

    struct sockaddr_in serverAddress;


    welcomeSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (welcomeSocket < 0) {
        print_error("socket() error", errno);
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

}

void Server::killServer() {
    int retVal = close(this->welcomeSocket);
}

int main() {
    std::cout << "Hello, World!" << std::endl;

    Server server =  Server();


    std::cout << "Bye, World!" << std::endl;


    return 0;
}