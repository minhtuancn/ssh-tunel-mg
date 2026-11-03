cmake_minimum_required(VERSION 3.25)
project(SSHTunnelManager
    VERSION 1.0.0
    LANGUAGES CXX C RC
    DESCRIPTION "Native Win32 SSH Tunnel Manager for Windows"
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

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

# Native GUI target.
add_executable(SSHTunnelManager WIN32 src/main.cpp
    src/ssh/ssh_process.cpp
    src/ssh/ssh_auth.cpp
    src/ssh/ssh_arguments.cpp
    src/ssh/ssh_known_hosts.cpp
    src/ssh/ssh_tunnel.cpp
    src/ssh/ssh_port_discovery.cpp
    src/ssh/ssh_systemd.cpp
    src/ssh/ssh_process_mgmt.cpp
    src/storage/sqlite_db.cpp
    third_party/sqlite3/sqlite3.c
    src/ui/main_window.cpp
    src/ui/resources.rc
)
target_compile_definitions(SSHTunnelManager PRIVATE
    _WIN32_WINNT=0x0A00
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
# windres (MinGW) splits -I arguments that contain spaces before handing them
# to cc1, which breaks builds from paths like "C:/.../code tunnel/...". The
# resource script only needs the system SDK headers, so keep these include
# directories out of the RC compile.
target_include_directories(SSHTunnelManager PRIVATE
    $<$<NOT:$<COMPILE_LANGUAGE:RC>>:${CMAKE_CURRENT_SOURCE_DIR}/src/ssh>
    $<$<NOT:$<COMPILE_LANGUAGE:RC>>:${CMAKE_CURRENT_SOURCE_DIR}/src/storage>
    $<$<NOT:$<COMPILE_LANGUAGE:RC>>:${CMAKE_CURRENT_SOURCE_DIR}/src/ui>
    $<$<NOT:$<COMPILE_LANGUAGE:RC>>:${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3>
)

target_link_libraries(SSHTunnelManager PRIVATE comctl32 shell32 ws2_32 iphlpapi)

# OpenSSL / bundled SSH dependency flags (conditionally set).
if(BUNDLE_OPENSSH)
    target_compile_definitions(SSHTunnelManager PRIVATE
        BUNDLE_OPENSSH
        _SSH_BINARY_PATH=\"/resources/openssh/openssh.exe\"
    )
    # Add the resources directory to the runtime search path so the
    # bundled openssh.exe can be found at runtime when the executable
    # is launched from the build directory.
    target_link_libraries(SSHTunnelManager PRIVATE
        advapi32
    )
endif()

if(MSVC)
    target_compile_options(SSHTunnelManager PRIVATE /W4 /WX /permissive- /guard:cf)
    target_link_options(SSHTunnelManager PRIVATE /DYNAMICBASE /NXCOMPAT /GUARD:CF)
elseif(MINGW)
    # Ship a self-contained executable: without static runtime linking the exe
    # needs libstdc++-6.dll / libgcc_s_seh-1.dll / libwinpthread-1.dll, which
    # are not present next to the binary and make double-click launch fail.
    target_link_options(SSHTunnelManager PRIVATE
        -municode
        -static
        -static-libgcc
        -static-libstdc++
    )
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)
install(TARGETS SSHTunnelManager RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})