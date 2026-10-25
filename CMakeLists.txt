cmake_minimum_required(VERSION 3.25)
project(SSHTunnelManager
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "Native Win32 SSH Tunnel Manager for Windows"
)

# Enforce x64 and Windows
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(FATAL_ERROR "This project targets Windows only")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "64-bit build required")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC warnings
if(MSVC)
    add_compile_options(
        /W4
        /WX
        /permissive-
        /guard:cf
        /D_CRT_SECURE_NO_WARNINGS
        /DWIN32_LEAN_AND_MEAN
        /DNOMINMAX
    )
    # Link options
    add_link_options(
        /DYNAMICBASE
        /NXCOMPAT
        /OPT:REF
        /OPT:ICF
        /GUARD:CF
        /MANIFEST:EMBED,ID=1
    )
endif()

# CMake policies
cmake_policy(SET CMP0077 NEW) # option() honors normal variables
cmake_policy(SET CMP0091 NEW) # FindPython/SWIG

# Release defaults
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Options
option(BUILD_TESTS "Build unit tests" ON)
option(BUNDLE_OPENSSH "Bundle OpenSSH binaries" ON)

# Native GUI target. SQLite is added with the storage task.
add_executable(SSHTunnelManager WIN32 src/main.cpp)
target_compile_definitions(SSHTunnelManager PRIVATE
    _WIN32_WINNT=0x0A00
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_link_libraries(SSHTunnelManager PRIVATE comctl32 shell32)

if(MSVC)
    target_compile_options(SSHTunnelManager PRIVATE /W4 /WX /permissive- /guard:cf)
    target_link_options(SSHTunnelManager PRIVATE /DYNAMICBASE /NXCOMPAT /GUARD:CF)
elseif(MINGW)
    target_link_options(SSHTunnelManager PRIVATE -municode)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)
install(TARGETS SSHTunnelManager RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})