# cmake_minimum_required(VERSION <specify CMake version here>)
project(Comm_Nets)

set(CMAKE_CXX_STANDARD 11)

#add_executable(Comm_Nets main.cpp client.cpp)
#add_executable(Comm_Nets main.cpp)
#add_executable(Comm_Nets client client.cpp)

set(CLIENT client.cpp)
set(SERVER server.cpp)
#set(SERVER main.cpp)
##set(IO whatsappio.cpp whatsappio.h)
#
##set(SERVER_SRC ${SERVER} ${IO})
set(SERVER_SRC ${SERVER})
add_executable(server ${SERVER_SRC})
#
##set(CLIENT_SRC ${CLIENT} ${IO})
set(CLIENT_SRC ${CLIENT})
add_executable(client ${CLIENT_SRC})
