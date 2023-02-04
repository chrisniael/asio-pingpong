cmake_minimum_required(VERSION 3.0.2)
project(asio-pingpong LANGUAGES CXX)

# 禁止 C++ assert terminate 程序
add_definitions(-DNDEBUG)

# Spdlog 使用外部 Fmt 库
add_definitions(-DSPDLOG_FMT_EXTERNAL)

# ASIO 不使用 Boost 库
add_definitions(-DASIO_STANDALONE)

# 生成编译命令文件，给 YCM 使用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 设置编译器参数
add_compile_options(-g)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)

#LINK_DIRECTORIES(${CMAKE_BINARY_DIR}/lib)

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
