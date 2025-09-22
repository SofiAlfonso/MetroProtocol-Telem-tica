cmake_minimum_required(VERSION 4.0)
project(MetroProtocol)

set(CMAKE_CXX_STANDARD 20)

add_executable(MetroProtocol
        Application/Server/server.c
        Application/Server/structs.h
        Application/Server/utils.h
        Application/Server/utils.c)
