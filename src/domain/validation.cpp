#include "validation.h"

#include <limits>

namespace {
bool ParseUnsigned(std::wstring_view input, uint64_t maxValue, uint64_t& value) {
    if (input.empty()) return false;
    value = 0;
    for (wchar_t c : input) {
        if (c < L'0' || c > L'9') return false;
        const uint64_t digit = static_cast<uint64_t>(c - L'0');
        if (value > (maxValue - digit) / 10) return false;
        value = value * 10 + digit;
    }
    return true;
}

bool ContainsUnsafeCharacter(std::wstring_view input) {
    for (wchar_t c : input) {
        if (c <= L' ' || c == L';' || c == L'&' || c == L'|' || c == L'>' ||
            c == L'<' || c == L'`' || c == L'$' || c == L'(' || c == L')') {
            return true;
        }
    }
    return false;
}
}

bool ParsePort(std::wstring_view input, uint16_t& out) {
    uint64_t value = 0;
    if (!ParseUnsigned(input, 65535, value) || value == 0) return false;
    out = static_cast<uint16_t>(value);
    return true;
}

bool ParsePid(std::wstring_view input, uint32_t& out) {
    uint64_t value = 0;
    if (!ParseUnsigned(input, std::numeric_limits<uint32_t>::max(), value) || value == 0) return false;
    out = static_cast<uint32_t>(value);
    return true;
}

bool ValidateHost(std::wstring_view input) {
    return !input.empty() && input.front() != L'-' && !ContainsUnsafeCharacter(input);
}

bool ValidateUnit(std::wstring_view input) {
    constexpr std::wstring_view suffix = L".service";
    return input.size() > suffix.size() && input.front() != L'-' &&
        input.substr(input.size() - suffix.size()) == suffix && !ContainsUnsafeCharacter(input);
}

std::wstring BuildWindowsCommandLineArgument(std::wstring_view argument) {
    const bool needsQuotes = argument.empty() || argument.find_first_of(L" \t\n\v\"") != std::wstring_view::npos;
    if (!needsQuotes) return std::wstring(argument);

    std::wstring quoted(1, L'\"');
    size_t backslashes = 0;
    for (wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
        } else if (character == L'\"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'\"');
            backslashes = 0;
        } else {
            quoted.append(backslashes, L'\\');
            quoted.push_back(character);
            backslashes = 0;
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}
