cmake_minimum_required(VERSION 3.10)
project(C-Agent C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

file(GLOB_RECURSE AGENT_SRCS "agent/*.c")
file(GLOB_RECURSE TOOL_SRCS "tools/*.c")
file(GLOB_RECURSE UI_SRCS "ui/*.c")
file(GLOB_RECURSE LIB_SRCS "libs/*.c")
file(GLOB ROOT_SRCS "*.c")

set(SOURCES ${AGENT_SRCS} ${TOOL_SRCS} ${UI_SRCS} ${LIB_SRCS} ${ROOT_SRCS})

add_executable(c-agent ${SOURCES})

target_include_directories(c-agent PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/libs)

find_package(OpenSSL REQUIRED)
target_link_libraries(c-agent PRIVATE OpenSSL::SSL OpenSSL::Crypto pthread)

target_compile_options(c-agent PRIVATE -Wall -Wextra -pedantic -g)