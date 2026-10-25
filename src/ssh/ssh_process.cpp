#include "ssh_process.h"

#include <windows.h>

#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace ssh_tunnel {
namespace {

struct HandleDeleter {
    void operator()(HANDLE h) const noexcept {
        if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
};

using ScopedHandle = std::unique_ptr<void, HandleDeleter>;

bool CreatePipePair(HANDLE& read, HANDLE& write) {
    SECURITY_ATTRIBUTES sa{sizeof(sa)};
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&read, &write, &sa, 0)) return false;
    if (!SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(read);
        CloseHandle(write);
        return false;
    }
    return true;
}

bool CloseHandleSafe(HANDLE h) {
    return h && h != INVALID_HANDLE_VALUE ? CloseHandle(h) : true;
}

} // namespace

ProcessResult RunSshProcess(const ProcessConfig& config, const std::atomic<bool>& cancelRequested) {
    ProcessResult result;

    if (config.executable.empty()) {
        result.error = L"Empty executable path";
        return result;
    }

    HANDLE stdoutRead = nullptr, stdoutWrite = nullptr;
    HANDLE stderrRead = nullptr, stderrWrite = nullptr;

    if (!CreatePipePair(stdoutRead, stdoutWrite) || !CreatePipePair(stderrRead, stderrWrite)) {
        result.error = L"CreatePipe failed";
        if (stdoutRead) CloseHandle(stdoutRead);
        if (stdoutWrite) CloseHandle(stdoutWrite);
        if (stderrRead) CloseHandle(stderrRead);
        if (stderrWrite) CloseHandle(stderrWrite);
        return result;
    }

    ScopedHandle hStdoutRead(stdoutRead);
    ScopedHandle hStdoutWrite(stdoutWrite);
    ScopedHandle hStderrRead(stderrRead);
    ScopedHandle hStderrWrite(stderrWrite);

    std::wstring cmdLine = L"\"" + config.executable + L"\"";
    for (const auto& arg : config.arguments) {
        bool needsQuotes = arg.empty() || arg.find_first_of(L" \t\n\v\"") != std::wstring::npos;
        if (!needsQuotes) {
            cmdLine += L" " + arg;
        } else {
            std::wstring quoted(1, L'\"');
            size_t backslashes = 0;
            for (wchar_t c : arg) {
                if (c == L'\\') {
                    ++backslashes;
                } else if (c == L'\"') {
                    quoted.append(backslashes * 2 + 1, L'\\');
                    quoted.push_back(L'\"');
                    backslashes = 0;
                } else {
                    quoted.append(backslashes, L'\\');
                    quoted.push_back(c);
                    backslashes = 0;
                }
            }
            quoted.append(backslashes * 2, L'\\');
            quoted.push_back(L'\"');
            cmdLine += L" " + quoted;
        }
    }

    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = stdoutWrite;
    si.hStdError = stderrWrite;

    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(config.executable.c_str(), cmdLine.data(), nullptr, nullptr,
                        TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        result.error = L"CreateProcessW failed: " + std::to_wstring(GetLastError());
        return result;
    }

    ScopedHandle hProcess(pi.hProcess);
    ScopedHandle hThread(pi.hThread);
    // Parent must close inherited writer copies; otherwise EOF is never observable.
    hStdoutWrite.reset();
    hStderrWrite.reset();

    // Drain pipes while process runs
    const size_t readBufSize = 65536;
    std::vector<char> readBuf(readBufSize);

    auto drainPipe = [&](HANDLE read, std::string& dest, size_t limit, bool& truncated, const char* name) {
        for (;;) {
            DWORD avail = 0;
            if (!PeekNamedPipe(read, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) return;
            DWORD toRead = static_cast<DWORD>(std::min<size_t>(avail, readBuf.size()));
            DWORD bytesRead = 0;
            if (!ReadFile(read, readBuf.data(), toRead, &bytesRead, nullptr) || bytesRead == 0) return;
            size_t capacity = limit - std::min(limit, dest.size());
            if (dest.size() + bytesRead > limit) {
                dest.append(readBuf.data(), capacity);
                truncated = true;
                // Keep draining to let the child finish, but drop the excess.
                continue;
            }
            dest.append(readBuf.data(), bytesRead);
        }
    };

    auto endTime = GetTickCount64() + config.timeoutMs;
    bool processExited = false;

    while (!processExited) {
        if (cancelRequested.load(std::memory_order_acquire)) {
            result.cancelled = true;
            TerminateProcess(pi.hProcess, 1);
            break;
        }

        DWORD waitResult = WaitForSingleObject(pi.hProcess, 10);
        if (waitResult == WAIT_OBJECT_0) {
            processExited = true;
        }

        // Drain available stdout/stderr until pipes are empty.
        drainPipe(stdoutRead, result.stdoutData, config.stdoutLimitBytes, result.truncatedStdout, "stdout");
        drainPipe(stderrRead, result.stderrData, config.stderrLimitBytes, result.truncatedStderr, "stderr");

        if (!processExited && GetTickCount64() >= endTime) {
            result.timedOut = true;
            TerminateProcess(pi.hProcess, 1);
            // Drain once more after terminate
            drainPipe(stdoutRead, result.stdoutData, config.stdoutLimitBytes, result.truncatedStdout, "stdout");
            drainPipe(stderrRead, result.stderrData, config.stderrLimitBytes, result.truncatedStderr, "stderr");
            break;
        }
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    result.exitCode = exitCode;
    return result;
}

} // namespace ssh_tunnel