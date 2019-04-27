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

int setup_socket() {
    struct sockaddr_in serverAddress;


    int welcomeSocket = socket(AF_INET, SOCK_STREAM, 0);
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

    return retVal;

}
int main() {
    std::cout << "Hello, World!" << std::endl;

    int clientfd = setup_socket();

    close(welcomeSocket);
    std::cout << "Bye, World!" << std::endl;


    return 0;
}