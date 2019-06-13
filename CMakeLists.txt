# cmake_minimum_required(VERSION <specify CMake version here>)
project(Comm_Nets)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_FLAGS -Wall -libverbs -D _GNU_SOURCE)
set(CMAKE_CXX_FLAGS -Wall -libverbs -D _GNU_SOURCE)


#set(CLIENT Ex1/client.cpp Ex1/shared.h)
#set(SERVER Ex1/server.cpp Ex1/shared.h)
#
#set(SERVER_SRC ${SERVER})
#add_executable(ex1server ${SERVER_SRC})
#
#set(CLIENT_SRC ${CLIENT})
#add_executable(ex1client ${CLIENT_SRC})
#
#
## Ex2
#set(EX2_CLIENT Ex2/client.cpp Ex2/shared.h)
#set(EX2_SERVER Ex2/server.cpp Ex2/shared.h)
#
#set(EX2_SERVER_SRC ${EX2_SERVER})
#add_executable(ex2server ${EX2_SERVER_SRC})
#
#set(EX2_CLIENT_SRC ${EX2_CLIENT})
#add_executable(ex2client ${EX2_CLIENT_SRC})
#
set(EX2_IB Ex2/ib_perf.c)
add_executable(ex2ib ${EX2_IB})

include_directories(/Users/netanel/Downloads/libibverbs-1.2.1/include)

set(EX3_SRC Ex3/kv_template.c)
add_executable(ex3 ${EX3_SRC})
#set(EX3_TMP_SRC Ex3/kv_template.c)
#add_executable(ex3template ${EX3_TMP_SRC})
