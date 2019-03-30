#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <string.h>
#include <chrono>

#define MAX_INCOMING_QUEUE 1
#define PORT_NUMBER 54321
#define MIN_WARMUP_CYCLES 100

void print_error(const std::string& function_name, int error_number) {
    printf("ERROR: %s %d.\n", function_name.c_str(), error_number);
    exit(EXIT_FAILURE);
}

class Client {
    int welcomeSocket;
    int serverfd;

public:
    //// C-tor
    Client();

    //// client actions

private:
    void killClient();

    void warmup();
};

Client::Client() {

    struct sockaddr_in clientAddress;
    struct sockaddr_in serverAddress;

    welcomeSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (welcomeSocket < 0) {
        print_error("socket() error", errno);
    }

    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(PORT_NUMBER);

    int retVal = inet_pton(AF_INET, "127.0.0.1", &serverAddress.sin_addr);
    if (retVal <= 0) {
        print_error("inet_pton()", errno);
    }

    serverfd = connect(welcomeSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
    if (serverfd < 0) {
        print_error("connect()", errno);
    }

}

void Client::killClient() {
    int retVal = close(this->welcomeSocket);

}


int main(int argc, char const *argv[]) {
    std::cout << "Hello, World!" << std::endl;
    Client client = Client();


    // TODO WARM-UP cycles
    // sending MIN cycles packets and counting

//    close(welcomeSocket);
    std::cout << "Bye, World!" << std::endl;
    return 0;
}