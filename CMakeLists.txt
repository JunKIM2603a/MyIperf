# Set the minimum required version of CMake.
# This resolves potential policy errors and ensures modern CMake features.
cmake_minimum_required(VERSION 3.15)

# Define the project name and supported languages.
project(IPEFTC LANGUAGES CXX)

# Set the C++ standard to C++17 and ensure it's required.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)



# --- nlohmann/json library setup ---
# Prefer a local version if available, otherwise fetch from GitHub.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/nlohmann/json.hpp")
    message(STATUS "Found local nlohmann/json.hpp")
    # Create an INTERFACE library for the local header-only library.
    # This makes linking consistent regardless of where the library comes from.
    add_library(nlohmann_json INTERFACE)
    target_include_directories(nlohmann_json INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
else()
    message(STATUS "Local nlohmann/json.hpp not found. Fetching from GitHub.")
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3 # Use a specific stable tag for reproducibility
    )
    # This makes the 'nlohmann_json' target available.
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# Define common source files that are always part of the build.
set(COMMON_SOURCE_FILES
    CLIHandler.cpp
    Config.cpp
    ConfigParser.cpp
    Logger.cpp
    main.cpp
    PacketGenerator.cpp
    PacketReceiver.cpp
    TestController.cpp
)

# Define common header files (for organization and IDE support).
set(COMMON_HEADER_FILES
    CLIHandler.h
    Config.h
    ConfigParser.h
    Logger.h
    NetworkInterface.h
    Protocol.h # Added missing protocol header
    PacketGenerator.h
    PacketReceiver.h
    TestController.h
)

# Add platform-specific source files based on the operating system.
if(WIN32)
    list(APPEND COMMON_SOURCE_FILES WinIOCPNetworkInterface.cpp)
    list(APPEND COMMON_HEADER_FILES WinIOCPNetworkInterface.h)
    message(STATUS "Building for Windows (including WinIOCPNetworkInterface.cpp)")
elseif(UNIX) # Covers Linux, macOS, etc.
    list(APPEND COMMON_SOURCE_FILES LinuxAsyncNetworkInterface.cpp)
    list(APPEND COMMON_HEADER_FILES LinuxAsyncNetworkInterface.h)
    message(STATUS "Building for Unix-like system (including LinuxAsyncNetworkInterface.cpp)")
else()
    message(FATAL_ERROR "Unsupported platform. Only Windows and Unix-like systems are supported.")
endif()

# Add the main executable.
add_executable(${CMAKE_PROJECT_NAME} ${COMMON_SOURCE_FILES})

# --- Compiler-specific Optimizations ---
# Add aggressive optimization flags for Release builds to maximize performance.
if(MSVC)
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:RelWithDebInfo>:/O2>
    )
    set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
else()
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:-O3 -flto>
        $<$<CONFIG:RelWithDebInfo>:-O2 -flto>
    )
    set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
endif()

# Add include directories for all source files.
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} # For all .h files in the root
)

# Link the executable against the nlohmann/json library.
# This works for both the local and fetched versions.
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE nlohmann_json)

# Add platform-specific settings.
if(WIN32)
    # On Windows, we need to link against the Winsock library for network operations.
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ws2_32 Mswsock)
    message(STATUS "Linking with ws2_32 and Mswsock for Windows.")

    # Define NOMINMAX to prevent min/max macro conflicts with the C++ standard library.
    # This is a cleaner approach than defining it in a source file.
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE NOMINMAX)
    message(STATUS "Adding NOMINMAX compile definition for Windows.")
endif()
# For Linux, epoll is part of glibc, so no explicit linking is typically required.

# Optional: Set output directory for executables
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")