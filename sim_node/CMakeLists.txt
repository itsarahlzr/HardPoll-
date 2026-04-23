cmake_minimum_required(VERSION 3.16)
project(hardpoll_sim_node CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(Threads REQUIRED)

add_library(hardpoll_core STATIC
    src/trace_logger.cpp
    src/sensor.cpp
    src/latency_injector.cpp
    src/adaptive_poller.cpp
    src/node.cpp
    src/control_server.cpp
)
target_include_directories(hardpoll_core PUBLIC include)
target_link_libraries(hardpoll_core PUBLIC Threads::Threads)

add_executable(hardpoll_sim_node src/main.cpp)
target_link_libraries(hardpoll_sim_node PRIVATE hardpoll_core)

enable_testing()
add_executable(hardpoll_tests tests/test_poller.cpp)
target_link_libraries(hardpoll_tests PRIVATE hardpoll_core)
add_test(NAME hardpoll_tests COMMAND hardpoll_tests)

install(TARGETS hardpoll_sim_node RUNTIME DESTINATION bin)
