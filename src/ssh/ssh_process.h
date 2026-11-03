#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ssh_tunnel {

struct ProcessConfig {
    std::wstring executable;
    std::vector<std::wstring> arguments;
    uint32_t timeoutMs = 30000;
    size_t stdoutLimitBytes = 4 * 1024 * 1024;
    size_t stderrLimitBytes = 256 * 1024;
};

struct ProcessResult {
    std::string stdoutData;
    std::string stderrData;
    uint32_t exitCode = static_cast<uint32_t>(-1);
    bool timedOut = false;
    bool cancelled = false;
    bool truncatedStdout = false;
    bool truncatedStderr = false;
    std::wstring error;
};

// Runs a child process and captures bounded stdout/stderr.
// A timeoutMs of 0 disables the timeout (used for long-lived SSH tunnels).
// Cancellation is signalled by either flag; both must outlive the call.
ProcessResult RunSshProcess(const ProcessConfig& config,
                            const std::atomic<bool>& cancelRequested,
                            const std::atomic<bool>* externalCancel = nullptr);

} // namespace ssh_tunnel