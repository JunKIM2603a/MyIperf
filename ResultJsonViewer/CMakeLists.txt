cmake_minimum_required(VERSION 3.15)

project(ResultJsonViewer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ResultJsonViewer
    src/main.cpp
)

target_include_directories(ResultJsonViewer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../MyIperf/third_party"
)

if(MSVC)
    target_compile_options(ResultJsonViewer PRIVATE /utf-8)
endif()
