cmake_minimum_required(VERSION 3.10)
project(TestRunner)

set(CMAKE_CXX_STANDARD 17)

set(SOURCES
    main.cpp
    IpeftcOutputParser.cpp
    Message.cpp
    ProcessManager.cpp
    ControlServer.cpp
    ControlClient.cpp
)

add_executable(TestRunner ${SOURCES})
target_include_directories(TestRunner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
    target_link_libraries(TestRunner ws2_32)
else()
    target_link_libraries(TestRunner pthread)
endif()
