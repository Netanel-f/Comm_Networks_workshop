#include "shared.h"


void print_error(const std::string& function_name, int error_number) {
    printf("ERROR: %s %d.\n", function_name.c_str(), error_number);
    exit(EXIT_FAILURE);
}

class Server {
    int welcomeSocket;
    int clientfd;

    char readBuf[WARMPUP_PACKET_SIZE + 1];


public:
    //// C-tor
    Server();

    void warmup_echo_back(bool * keepEcho);
    void echo();
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
    std::cout << "Bye, World!" << std::endl;
    int retVal = close(this->clientfd);
    std::cout << "close output: " << retVal << std::endl;
    retVal = close(this->welcomeSocket);
    std::cout << "close output: " << retVal << std::endl;

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
    int echoCounter = 0;
    while (keepEcho) {
        ssize_t retVal = recv(this->clientfd, this->readBuf, (size_t) WARMPUP_PACKET_SIZE, 0);
        if (DEBUG) { std::cout << "warmup recieved size: " << retVal<< std::endl; }
        if (retVal < 0) {
            print_error("recv() failed", errno);
        }
        if (retVal == 0) {
            keepEcho = false;
            if (DEBUG) { std::cout << "received total of " << echoCounter << std::endl; }
            return;
        }
        echoCounter++;
        retVal = send(this->clientfd, this->readBuf, (size_t) retVal, 0);
        if (DEBUG) { std::cout << "warmup sent size: " << retVal << std::endl; }
        if (retVal < 0) {
            print_error("send() failed", errno);
        }
    }

}

int main() {

    Server server =  Server();
    server.echo();
//    bool keepEcho = true;
//    int echoCounter = 0;
//    while (keepEcho) {
//        if (DEBUG) { std::cout << "warmup_echo_back #" << echoCounter << std::endl; }
//        server.warmup_echo_back(&keepEcho);
//        echoCounter++;
//    }

    server.killServer();


    return 0;
}