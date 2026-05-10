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

# ── Platform-specific configuration ────────────────────

if(WIN32)
  # Windows: use native threads (CRITICAL_SECTION / CONDITION_VARIABLE via compat.h)
  # Link winsock2 for socket functions
  target_link_libraries(c-agent PRIVATE ws2_32)

  # OpenSSL on Windows — assumes vcpkg or manual install
  find_package(OpenSSL QUIET)
  if(OPENSSL_FOUND)
    target_link_libraries(c-agent PRIVATE OpenSSL::SSL OpenSSL::Crypto)
  else()
    message(WARNING "OpenSSL not found — HTTPS support will be disabled. "
                    "Install via vcpkg: vcpkg install openssl")
  endif()

  # MSVC-specific flags
  if(MSVC)
    target_compile_options(c-agent PRIVATE /W4)
    target_compile_definitions(c-agent PRIVATE _CRT_SECURE_NO_WARNINGS)
  else()
    # MinGW / clang on Windows
    target_compile_options(c-agent PRIVATE -Wall -Wextra -pedantic -g)
  endif()

else()
  # Linux / macOS / POSIX
  find_package(OpenSSL REQUIRED)
  target_link_libraries(c-agent PRIVATE OpenSSL::SSL OpenSSL::Crypto pthread)

  target_compile_options(c-agent PRIVATE -Wall -Wextra -pedantic -g)
  target_compile_definitions(c-agent PRIVATE _XOPEN_SOURCE=700)
endif()
