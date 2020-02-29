cmake_minimum_required(VERSION 2.8.0)
add_compile_options(-O2)

if(MSVC)
    set(CMAKE_BUILD_TYPE Release)
else()
    add_compile_options(-std=c++11)
endif()

include_directories(tinygc)
add_executable(tinygc_test test/main.cpp tinygc/tinygc.cpp)