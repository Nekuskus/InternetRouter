cmake_minimum_required(VERSION 3.0)
project(InternetRouter)
set(CXX_STANDARD 20)
set(THREADS_PREFER_PTHREAD_FLAG ON)
add_executable(InternetRouter main.cpp)
find_library(wiringPi_LIB wiringPi)
target_link_libraries(InternetRouter ${wiringPi_LIB})
find_package(Threads)
target_link_libraries(InternetRouter Threads::Threads)
find_library(pi-gpio_LIB pi-gpio)
target_link_libraries(InternetRouter ${pi-gpio_LIB})
