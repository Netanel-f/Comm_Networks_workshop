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
    struct sockaddr_in clientAddress;
    struct sockaddr_in serverAddress;

    int welcomeSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (welcomeSocket < 0) {
        print_error("socket() error", errno);
    }

    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(PORT_NUMBER);

    int retVal = inet_pton(AF_INET, "127.0.0.1", &serverAddress.sin_addr)
    if (retVal <= 0) {
        print_error("inet_pton()", errno);
    }

    retVal = connect(welcomeSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)))
    if (retVal < 0) {
        print_error("connect()", errno);
    }

    return welcomeSocket
}


int main(int argc, char const *argv[]) {
    std::cout << "Hello, World!" << std::endl;

    int cSocket = setup_socket();


    return 0;
}