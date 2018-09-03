cmake_minimum_required(VERSION 3.0.2)
project(asio-pingpong)

message(STATUS "CMake version: " ${CMAKE_VERSION})
if(NOT ${CMAKE_VERSION} VERSION_LESS "3.2")
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
else()
    message(STATUS "Checking compiler flags for C++11 support.")
    # Set C++11 support flags for various compilers
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-std=c++11" COMPILER_SUPPORTS_CXX11)
    check_cxx_compiler_flag("-std=c++0x" COMPILER_SUPPORTS_CXX0X)
    if(COMPILER_SUPPORTS_CXX11)
        message(STATUS "C++11 is supported.")
        if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -stdlib=libc++")
        else()
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
        endif()
    elseif(COMPILER_SUPPORTS_CXX0X)
        message(STATUS "C++0x is supported.")
        if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x -stdlib=libc++")
        else()
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
        endif()
    else()
        message(STATUS "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
    endif()
endif()

# 禁止 C++ assert terminate 程序
ADD_DEFINITIONS(-DNDEBUG)

# Spdlog 使用外部 Fmt 库
ADD_DEFINITIONS(-DSPDLOG_FMT_EXTERNAL)

# ASIO 不使用 Boost 库
ADD_DEFINITIONS(-DASIO_STANDALONE)

# 生成编译命令文件，给 YCM 使用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
  message(STATUS "Clang")
  ADD_COMPILE_OPTIONS(-O2 -g -pthread)
elseif(${CMAKE_CXX_COMPILER_ID} STREQUAL "AppleClang")
  message(STATUS "AppleClang")
  ADD_COMPILE_OPTIONS(-O2 -g -pthread)
elseif(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
  message(STATUS "GNU")
  ADD_COMPILE_OPTIONS(-O2 -g -pthread)
elseif(${CMAKE_CXX_COMPILER_ID} STREQUAL "MSVC")
  message(STATUS "MSVC")
  ADD_COMPILE_OPTIONS(/O2)
endif()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)

#LINK_DIRECTORIES(${CMAKE_BINARY_DIR}/lib)

message(STATUS "queue-service")
message(STATUS "CMAKE_SOURCE_DIR         = ${CMAKE_SOURCE_DIR}")
message(STATUS "CMAKE_BINARY_DIR         = ${CMAKE_BINARY_DIR}")
message(STATUS "PROJECT_SOURCE_DIR       = ${PROJECT_SOURCE_DIR}")
message(STATUS "PROJECT_BINARY_DIR       = ${PROJECT_BINARY_DIR}")
message(STATUS "CMAKE_CURRENT_SOURCE_DIR = ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "CMAKE_CURRENT_BINARY_DIR = ${CMAKE_CURRENT_BINARY_DIR}")

include_directories(${CMAKE_SOURCE_DIR}/opt/client)
include_directories(${CMAKE_SOURCE_DIR}/opt/server)
include_directories(${CMAKE_SOURCE_DIR}/opt/asio/asio/include)

aux_source_directory(${CMAKE_SOURCE_DIR}/client CLIENT_SOURCE_LIST)

aux_source_directory(${CMAKE_SOURCE_DIR}/server SERVER_SOURCE_LIST)

add_executable(client ${CLIENT_SOURCE_LIST})
add_executable(server ${SERVER_SOURCE_LIST})
if(NOT WIN32)
  target_link_libraries(client pthread)
  target_link_libraries(server pthread)
endif()
