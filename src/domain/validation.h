#pragma once

#include <cstdint>
#include <string>
#include <string_view>

bool ParsePort(std::wstring_view input, uint16_t& out);
bool ParsePid(std::wstring_view input, uint32_t& out);
bool ValidateHost(std::wstring_view input);
bool ValidateUnit(std::wstring_view input);
std::wstring BuildWindowsCommandLineArgument(std::wstring_view argument);
