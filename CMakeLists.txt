# cmake_minimum_required(VERSION <specify CMake version here>)
project(Comm_Nets)

set(CMAKE_CXX_STANDARD 11)


set(CLIENT client.cpp shared.h)
set(SERVER server.cpp shared.h)

set(SERVER_SRC ${SERVER})
add_executable(server ${SERVER_SRC})

set(CLIENT_SRC ${CLIENT})
add_executable(client ${CLIENT_SRC})
