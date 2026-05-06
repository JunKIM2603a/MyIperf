cmake_minimum_required(VERSION 3.15)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" MYIPERF_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
project(IPEFTC VERSION ${MYIPERF_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MYIPERF_ENABLE_DEBUG_LOGS "Enable verbose Debug: log lines" OFF)

set(MYIPERF_PRODUCT_NAME "IPEFTC")
execute_process(
    COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --short HEAD
    OUTPUT_VARIABLE MYIPERF_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
execute_process(
    COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --abbrev-ref HEAD
    OUTPUT_VARIABLE MYIPERF_GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
execute_process(
    COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" status --porcelain
    OUTPUT_VARIABLE MYIPERF_GIT_STATUS
    ERROR_QUIET
)
if(NOT MYIPERF_GIT_COMMIT)
    set(MYIPERF_GIT_COMMIT "unknown")
endif()
if(NOT MYIPERF_GIT_BRANCH)
    set(MYIPERF_GIT_BRANCH "unknown")
endif()
if(MYIPERF_GIT_STATUS STREQUAL "")
    set(MYIPERF_GIT_DIRTY false)
else()
    set(MYIPERF_GIT_DIRTY true)
endif()
string(TIMESTAMP MYIPERF_BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%S%z")
string(REGEX REPLACE "([+-][0-9][0-9])([0-9][0-9])$" "\\1:\\2" MYIPERF_BUILD_TIMESTAMP "${MYIPERF_BUILD_TIMESTAMP}")

set(MYIPERF_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${MYIPERF_GENERATED_INCLUDE_DIR}/myiperf")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VersionGenerated.h.in"
    "${MYIPERF_GENERATED_INCLUDE_DIR}/myiperf/VersionGenerated.h"
    @ONLY
)

set(MYIPERF_OUTPUT_BIN_DIR "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(MYIPERF_OUTPUT_LIB_DIR "${CMAKE_BINARY_DIR}/lib/$<CONFIG>")

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)

set(MYIPERF_CORE_PUBLIC_HEADERS
    include/myiperf/Config.h
    include/myiperf/ConfigParser.h
    include/myiperf/CoroutineSupport.h
    include/myiperf/Logger.h
    include/myiperf/NetworkInterface.h
    include/myiperf/Protocol.h
    include/myiperf/RunOptions.h
    include/myiperf/TestController.h
    include/myiperf/TestRunResult.h
    include/myiperf/Version.h
)

set(MYIPERF_CORE_PRIVATE_HEADERS
    src/myiperf/ClientTestSession.h
    src/myiperf/ControlChannel.h
    src/myiperf/ControlMessageBus.h
    src/myiperf/ControlProtocol.h
    src/myiperf/NetworkInterfaceFactory.h
    src/myiperf/PacketDispatcher.h
    src/myiperf/PacketGenerator.h
    src/myiperf/PacketReceiveStats.h
    src/myiperf/PacketReceiver.h
    src/myiperf/PacketStreamParser.h
    src/myiperf/ParsedPacket.h
    src/myiperf/ResultEventSink.h
    src/myiperf/ServerTestSession.h
    src/myiperf/TestSessionContext.h
)

set(MYIPERF_CORE_SOURCES
    src/myiperf/ClientTestSession.cpp
    src/myiperf/Config.cpp
    src/myiperf/ConfigParser.cpp
    src/myiperf/ControlChannel.cpp
    src/myiperf/ControlMessageBus.cpp
    src/myiperf/ControlProtocol.cpp
    src/myiperf/Logger.cpp
    src/myiperf/NetworkInterfaceFactory.cpp
    src/myiperf/PacketDispatcher.cpp
    src/myiperf/PacketGenerator.cpp
    src/myiperf/PacketReceiveStats.cpp
    src/myiperf/PacketReceiver.cpp
    src/myiperf/PacketStreamParser.cpp
    src/myiperf/ResultEventSink.cpp
    src/myiperf/ServerTestSession.cpp
    src/myiperf/TestController.cpp
    src/myiperf/Version.cpp
)

if(WIN32)
    list(APPEND MYIPERF_CORE_SOURCES src/myiperf/platform/WinIOCPNetworkInterface.cpp)
    list(APPEND MYIPERF_CORE_PRIVATE_HEADERS src/myiperf/platform/WinIOCPNetworkInterface.h)
    message(STATUS "Building myiperf_core for Windows")
elseif(UNIX)
    list(APPEND MYIPERF_CORE_SOURCES src/myiperf/platform/LinuxAsyncNetworkInterface.cpp)
    list(APPEND MYIPERF_CORE_PRIVATE_HEADERS src/myiperf/platform/LinuxAsyncNetworkInterface.h)
    message(STATUS "Building myiperf_core for Unix-like system")
else()
    message(FATAL_ERROR "Unsupported platform. Only Windows and Unix-like systems are supported.")
endif()

add_library(myiperf_core STATIC
    ${MYIPERF_CORE_PUBLIC_HEADERS}
    ${MYIPERF_CORE_PRIVATE_HEADERS}
    ${MYIPERF_CORE_SOURCES}
)
add_library(MyIperf::core ALIAS myiperf_core)

set_target_properties(myiperf_core PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${MYIPERF_OUTPUT_LIB_DIR}"
)

target_include_directories(myiperf_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
        "${MYIPERF_GENERATED_INCLUDE_DIR}"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/myiperf"
)

target_link_libraries(myiperf_core PUBLIC nlohmann_json)
target_compile_definitions(myiperf_core PRIVATE "MYIPERF_BUILD_CONFIG=\"$<CONFIG>\"")

if(MYIPERF_ENABLE_DEBUG_LOGS)
    target_compile_definitions(myiperf_core PRIVATE MYIPERF_ENABLE_DEBUG_LOGS=1)
endif()

if(WIN32)
    target_link_libraries(myiperf_core PRIVATE ws2_32 Mswsock)
    target_compile_definitions(myiperf_core PRIVATE NOMINMAX)
endif()

add_executable(IPEFTC
    app/ipeftc/main.cpp
    app/ipeftc/CLIHandler.cpp
    app/ipeftc/CLIHandler.h
)

set_target_properties(IPEFTC PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${MYIPERF_OUTPUT_BIN_DIR}"
)

target_include_directories(IPEFTC PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/app/ipeftc"
)

target_link_libraries(IPEFTC PRIVATE MyIperf::core)

foreach(target myiperf_core IPEFTC)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /utf-8
            $<$<CONFIG:Release>:/O2>
            $<$<CONFIG:RelWithDebInfo>:/O2>
        )
        set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    else()
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Release>:-O3 -flto>
            $<$<CONFIG:RelWithDebInfo>:-O2 -flto>
        )
        set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    endif()
endforeach()
