#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <strings.h>

int main() {
    std::cout << "Hello, World!" << std::endl;

    struct sockaddr_in sa;
    struct hostent* hostEnt;

    int retVal = gethostname(srvName, WA_MAX_NAME);
    if (retVal < 0) {
        std::cerr << "gethostname", errno);
    }

    bzero(&sa, sizeof(struct sockaddr_in));
    hostEnt = gethostbyname(srvName);
    if (hostEnt == nullptr) {
        std::cerr << "gethostbyname", errno);
    }

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = (sa_family_t) hostEnt->h_addrtype;
    memcpy(&sa.sin_addr, hostEnt->h_addr, (size_t) hostEnt->h_length);
    sa.sin_port = htons(portNumber);


    int welcomeSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (welcomeSocket < 0) {
        std::cerr << "socket() error" << std::endl;
    }




    return 0;
}