cmake_minimum_required(VERSION 3.10)
project(TestRunner2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include parent directory for nlohmann/json
include_directories(${CMAKE_SOURCE_DIR}/..)

# Source files
set(SOURCES
    main.cpp
    Message.cpp
    ProcessManager.cpp
    ControlServer.cpp
    ControlClient.cpp
)

# Header files (for IDE visibility)
set(HEADERS
    Protocol.h
    Message.h
    ProcessManager.h
    ControlServer.h
    ControlClient.h
)

# Create executable
add_executable(TestRunner2 ${SOURCES} ${HEADERS})

# Link Winsock library on Windows
if (WIN32)
    target_link_libraries(TestRunner2 ws2_32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
endif()

# For Linux (future support)
if (UNIX)
    target_link_libraries(TestRunner2 pthread)
endif()

