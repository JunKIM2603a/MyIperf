cmake_minimum_required(VERSION 3.15)

project(ResultPipeVerifier VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ResultPipeVerifier
    src/main.cpp
)

target_include_directories(ResultPipeVerifier PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../MyIperf/third_party"
)

if(WIN32)
    target_compile_definitions(ResultPipeVerifier PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
else()
    target_link_libraries(ResultPipeVerifier PRIVATE pthread)
endif()

if(MSVC)
    target_compile_options(ResultPipeVerifier PRIVATE /utf-8)
endif()
