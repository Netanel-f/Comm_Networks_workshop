# cmake_minimum_required(VERSION <specify CMake version here>)
project(Comm_Nets)

set(CMAKE_CXX_STANDARD 11)


set(CLIENT client.cpp shared.h)
set(SERVER server.cpp shared.h)

set(SERVER_SRC ${SERVER})
add_executable(server ${SERVER_SRC})

set(CLIENT_SRC ${CLIENT})
add_executable(client ${CLIENT_SRC})


# Ex2
set(EX2_CLIENT Ex2/client.cpp Ex2/shared.h)
set(EX2_SERVER Ex2/server.cpp Ex2/shared.h)

set(EX2_SERVER_SRC ${EX2_SERVER})
add_executable(ex2server ${EX2_SERVER_SRC})

set(EX2_CLIENT_SRC ${EX2_CLIENT})
add_executable(ex2client ${EX2_CLIENT_SRC})