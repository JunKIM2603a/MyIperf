cmake_minimum_required(VERSION 3.10)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" TESTRUNNER_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
project(TestRunner VERSION ${TESTRUNNER_VERSION})

set(CMAKE_CXX_STANDARD 17)

set(TESTRUNNER_PRODUCT_NAME "TestRunner")
execute_process(
    COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --short HEAD
    OUTPUT_VARIABLE TESTRUNNER_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
execute_process(
    COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --abbrev-ref HEAD
    OUTPUT_VARIABLE TESTRUNNER_GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
execute_process(
    COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" status --porcelain
    OUTPUT_VARIABLE TESTRUNNER_GIT_STATUS
    ERROR_QUIET
)
if(NOT TESTRUNNER_GIT_COMMIT)
    set(TESTRUNNER_GIT_COMMIT "unknown")
endif()
if(NOT TESTRUNNER_GIT_BRANCH)
    set(TESTRUNNER_GIT_BRANCH "unknown")
endif()
if(TESTRUNNER_GIT_STATUS STREQUAL "")
    set(TESTRUNNER_GIT_DIRTY false)
else()
    set(TESTRUNNER_GIT_DIRTY true)
endif()
string(TIMESTAMP TESTRUNNER_BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%S%z")
string(REGEX REPLACE "([+-][0-9][0-9])([0-9][0-9])$" "\\1:\\2" TESTRUNNER_BUILD_TIMESTAMP "${TESTRUNNER_BUILD_TIMESTAMP}")

set(TESTRUNNER_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${TESTRUNNER_GENERATED_INCLUDE_DIR}/testrunner")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VersionGenerated.h.in"
    "${TESTRUNNER_GENERATED_INCLUDE_DIR}/testrunner/VersionGenerated.h"
    @ONLY
)

set(SOURCES
    main.cpp
    IpeftcOutputParser.cpp
    Message.cpp
    ProcessManager.cpp
    ControlServer.cpp
    ControlClient.cpp
    Version.cpp
)

add_executable(TestRunner ${SOURCES})
target_include_directories(TestRunner PRIVATE
    ${TESTRUNNER_GENERATED_INCLUDE_DIR}
)
target_compile_definitions(TestRunner PRIVATE "TESTRUNNER_BUILD_CONFIG=\"$<CONFIG>\"")

if(WIN32)
    target_link_libraries(TestRunner ws2_32)
else()
    target_link_libraries(TestRunner pthread)
endif()
