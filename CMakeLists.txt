cmake_minimum_required(VERSION 3.17)
project(fsscan)
set(CMAKE_CXX_STANDARD 20)

add_executable(file-finder main.cpp fswalker.cpp fswalker.h chan.h util.h)
