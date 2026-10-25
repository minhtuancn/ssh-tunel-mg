add_executable(self_check unit/self_check.cpp ../src/domain/validation.cpp ../src/linux/commands.cpp)
target_include_directories(self_check PRIVATE ../src/domain ../src/linux)
add_test(NAME self_check COMMAND self_check)
