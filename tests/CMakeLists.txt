# Release builds define NDEBUG, which turns assert() into a no-op and makes
# assertion-based tests silently vacuous. Keep assertions enabled for tests.
if(MSVC)
    add_compile_options(/UNDEBUG)
else()
    add_compile_options(-UNDEBUG)
endif()

add_executable(self_check unit/self_check.cpp ../src/domain/validation.cpp ../src/linux/commands.cpp)
target_include_directories(self_check PRIVATE ../src/domain ../src/linux)
add_test(NAME self_check COMMAND self_check)

add_executable(process_helper unit/process_helper.cpp)
target_include_directories(process_helper PRIVATE ../src/ssh)

add_executable(process_runner_test unit/process_runner_test.cpp ../src/ssh/ssh_process.cpp)
target_include_directories(process_runner_test PRIVATE ../src/ssh)
add_dependencies(process_runner_test process_helper)
add_test(NAME process_runner_test COMMAND process_runner_test)
# Run next to the helper binary instead of relying on a stale ../build output.
set_tests_properties(process_runner_test PROPERTIES
    WORKING_DIRECTORY $<TARGET_FILE_DIR:process_helper>)

# SSH Auth tests
add_executable(test_ssh_auth unit/test_ssh_auth.cpp ../src/ssh/ssh_auth.cpp)
target_include_directories(test_ssh_auth PRIVATE ../src/ssh)
add_test(NAME test_ssh_auth COMMAND test_ssh_auth)

# SSH Arguments tests
add_executable(test_ssh_arguments unit/test_ssh_arguments.cpp ../src/ssh/ssh_arguments.cpp)
target_include_directories(test_ssh_arguments PRIVATE ../src/ssh)
add_test(NAME test_ssh_arguments COMMAND test_ssh_arguments)

# SSH Known Hosts tests
add_executable(test_ssh_known_hosts unit/test_ssh_known_hosts.cpp ../src/ssh/ssh_known_hosts.cpp ../src/ssh/ssh_auth.cpp ../src/ssh/ssh_arguments.cpp ../src/ssh/ssh_process.cpp)
target_include_directories(test_ssh_known_hosts PRIVATE ../src/ssh)
target_link_libraries(test_ssh_known_hosts PRIVATE ws2_32 iphlpapi bcrypt)
add_test(NAME test_ssh_known_hosts COMMAND test_ssh_known_hosts)

# SSH Tunnel tests
add_executable(test_ssh_tunnel unit/test_ssh_tunnel.cpp ../src/ssh/ssh_tunnel.cpp ../src/ssh/ssh_arguments.cpp ../src/ssh/ssh_auth.cpp ../src/ssh/ssh_process.cpp ../src/ssh/ssh_known_hosts.cpp)
target_include_directories(test_ssh_tunnel PRIVATE ../src/ssh)
target_link_libraries(test_ssh_tunnel PRIVATE ws2_32 iphlpapi bcrypt)
add_test(NAME test_ssh_tunnel COMMAND test_ssh_tunnel)

# SSH Port Discovery tests
add_executable(test_ssh_port_discovery unit/test_ssh_port_discovery.cpp ../src/ssh/ssh_port_discovery.cpp ../src/ssh/ssh_tunnel.cpp ../src/ssh/ssh_arguments.cpp ../src/ssh/ssh_auth.cpp ../src/ssh/ssh_process.cpp ../src/ssh/ssh_known_hosts.cpp)
target_include_directories(test_ssh_port_discovery PRIVATE ../src/ssh)
target_link_libraries(test_ssh_port_discovery PRIVATE ws2_32 iphlpapi bcrypt)
add_test(NAME test_ssh_port_discovery COMMAND test_ssh_port_discovery)

# SSH Systemd tests
add_executable(test_ssh_systemd unit/test_ssh_systemd.cpp ../src/ssh/ssh_systemd.cpp ../src/ssh/ssh_tunnel.cpp ../src/ssh/ssh_arguments.cpp ../src/ssh/ssh_auth.cpp ../src/ssh/ssh_process.cpp ../src/ssh/ssh_known_hosts.cpp)
target_include_directories(test_ssh_systemd PRIVATE ../src/ssh)
target_link_libraries(test_ssh_systemd PRIVATE ws2_32 iphlpapi advapi32 bcrypt)
add_test(NAME test_ssh_systemd COMMAND test_ssh_systemd)

# SSH Process Management tests
add_executable(test_ssh_process_mgmt unit/test_ssh_process_mgmt.cpp ../src/ssh/ssh_process_mgmt.cpp ../src/ssh/ssh_tunnel.cpp ../src/ssh/ssh_arguments.cpp ../src/ssh/ssh_auth.cpp ../src/ssh/ssh_process.cpp ../src/ssh/ssh_known_hosts.cpp)
target_include_directories(test_ssh_process_mgmt PRIVATE ../src/ssh)
target_link_libraries(test_ssh_process_mgmt PRIVATE ws2_32 iphlpapi bcrypt)
add_test(NAME test_ssh_process_mgmt COMMAND test_ssh_process_mgmt)

# SQLite DB tests
add_executable(test_sqlite_db unit/test_sqlite_db.cpp ../src/storage/sqlite_db.cpp ../third_party/sqlite3/sqlite3.c)
target_include_directories(test_sqlite_db PRIVATE ../src/storage ../third_party/sqlite3)
add_test(NAME test_sqlite_db COMMAND test_sqlite_db)