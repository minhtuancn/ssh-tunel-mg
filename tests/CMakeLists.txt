add_executable(self_check unit/self_check.cpp ../src/domain/validation.cpp ../src/linux/commands.cpp)
target_include_directories(self_check PRIVATE ../src/domain ../src/linux)
add_test(NAME self_check COMMAND self_check)

add_executable(process_helper unit/process_helper.cpp)
target_include_directories(process_helper PRIVATE ../src/ssh)

add_executable(process_runner_test unit/process_runner_test.cpp ../src/ssh/ssh_process.cpp)
target_include_directories(process_runner_test PRIVATE ../src/ssh)
add_dependencies(process_runner_test process_helper)
add_test(NAME process_runner_test COMMAND process_runner_test)
set_tests_properties(process_runner_test PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
