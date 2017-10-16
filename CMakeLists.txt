cmake_minimum_required(VERSION 3.8)
project(Lab2)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
        gbn.c
        gbn.h
        receiver.c
        sender.c)

add_executable(Lab2 ${SOURCE_FILES})