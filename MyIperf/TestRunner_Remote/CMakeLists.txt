cmake_minimum_required(VERSION 3.10)
project(TestRunner_Remote)

set(CMAKE_CXX_STANDARD 17)

# Include nlohmann json
include_directories(../nlohmann)

set(SOURCES
    main.cpp
    Message.cpp
    ProcessManager.cpp
    ControlServer.cpp
    ControlClient.cpp
)

add_executable(TestRunner2 ${SOURCES})

if(WIN32)
    target_link_libraries(TestRunner2 ws2_32)
endif()
