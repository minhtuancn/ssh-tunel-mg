#include "ssh_process.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAIL: " << name << '\n';
        ++failures;
    }
}

void TestBasicRun() {
    ssh_tunnel::ProcessConfig cfg;
    cfg.executable = L"build/bin/process_helper.exe";
    cfg.arguments = {L"exit"};
    cfg.timeoutMs = 2000;
    std::atomic<bool> cancel{false};

    auto result = ssh_tunnel::RunSshProcess(cfg, cancel);
    Check(result.exitCode == 0, "basic exit succeeds");
    Check(!result.timedOut, "not timed out");
    Check(!result.cancelled, "not cancelled");
    Check(result.stdoutData.empty(), "stdout empty");
    Check(result.stderrData.empty(), "stderr empty");
}

void TestExitCodePropagation() {
    ssh_tunnel::ProcessConfig cfg;
    cfg.executable = L"build/bin/process_helper.exe";
    cfg.arguments = {L"exit-code", L"42"};
    cfg.timeoutMs = 2000;
    std::atomic<bool> cancel{false};

    auto result = ssh_tunnel::RunSshProcess(cfg, cancel);
    Check(result.exitCode == 42, "exit code propagated");
}

void TestStdoutCapture() {
    ssh_tunnel::ProcessConfig cfg;
    cfg.executable = L"build/bin/process_helper.exe";
    cfg.arguments = {L"stdout", L"100"};
    cfg.timeoutMs = 2000;
    std::atomic<bool> cancel{false};

    auto result = ssh_tunnel::RunSshProcess(cfg, cancel);
    Check(result.exitCode == 0, "exit ok");
    Check(result.stdoutData.size() == 100, "stdout 100 bytes");
    Check(std::all_of(result.stdoutData.begin(), result.stdoutData.end(),
                      [](char c) { return c == 'A'; }), "stdout all 'A'");
}

void TestStderrCapture() {
    ssh_tunnel::ProcessConfig cfg;
    cfg.executable = L"build/bin/process_helper.exe";
    cfg.arguments = {L"stderr", L"50"};
    cfg.timeoutMs = 2000;
    std::atomic<bool> cancel{false};

    auto result = ssh_tunnel::RunSshProcess(cfg, cancel);
    Check(result.exitCode == 0, "exit ok");
    Check(result.stderrData.size() == 50, "stderr 50 bytes");
    Check(std::all_of(result.stderrData.begin(), result.stderrData.end(),
                      [](char c) { return c == 'B'; }), "stderr all 'B'");
}

void TestBothStreams() {
    ssh_tunnel::ProcessConfig cfg;
    cfg.executable = L"build/bin/process_helper.exe";
    cfg.arguments = {L"both", L"75"};
    cfg.timeoutMs = 2000;
    std::atomic<bool> cancel{false};

    auto result = ssh_tunnel::RunSshProcess(cfg, cancel);
    Check(result.exitCode == 0, "exit ok");
    Check(result.stdoutData.size() == 75, "stdout 75 bytes");
    Check(result.stderrData.size() == 75, "stderr 75 bytes");
}

void TestInfiniteOutputTimeout() {
    ssh_tunnel::ProcessConfig cfg;
    cfg.executable = L"build/bin/process_helper.exe";
    cfg.arguments = {L"infinite"};
    cfg.timeoutMs = 200;
    cfg.stdoutLimitBytes = 1024;
    std::atomic<bool> cancel{false};

    auto result = ssh_tunnel::RunSshProcess(cfg, cancel);
    Check(result.timedOut, "infinite output obeys timeout");
    Check(result.stdoutData.size() <= cfg.stdoutLimitBytes, "infinite output stays bounded");
}

void TestTimeout() {
    ssh_tunnel::ProcessConfig cfg;
    cfg.executable = L"build/bin/process_helper.exe";
    cfg.arguments = {L"sleep", L"2000"};
    cfg.timeoutMs = 200;  // much shorter than sleep
    std::atomic<bool> cancel{false};

    auto result = ssh_tunnel::RunSshProcess(cfg, cancel);
    Check(result.timedOut, "timed out flagged");
    Check(result.cancelled == false, "not cancelled");
}

void TestCancel() {
    ssh_tunnel::ProcessConfig cfg;
    cfg.executable = L"build/bin/process_helper.exe";
    cfg.arguments = {L"sleep", L"2000"};
    cfg.timeoutMs = 5000;  // long timeout
    std::atomic<bool> cancel{false};

    std::thread canceller([&cancel] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        cancel.store(true, std::memory_order_release);
    });
    auto result = ssh_tunnel::RunSshProcess(cfg, cancel);
    canceller.join();
    Check(result.cancelled, "cancel flag terminates process");
    Check(!result.timedOut, "cancel distinct from timeout");
}

void TestTruncation() {
    ssh_tunnel::ProcessConfig cfg;
    cfg.executable = L"build/bin/process_helper.exe";
    // 2 MiB exceeds this test's 1 MiB stdout limit.
    cfg.stdoutLimitBytes = 1024 * 1024;
    cfg.arguments = {L"stdout", L"2097152"};  // 2 MiB
    cfg.timeoutMs = 10000;
    std::atomic<bool> cancel{false};

    auto result = ssh_tunnel::RunSshProcess(cfg, cancel);
    Check(result.truncatedStdout, "stdout truncated flagged");
    Check(result.stdoutData.size() <= 1024 * 1024, "stdout not exceed limit");
}

} // namespace

int main() {
    TestBasicRun();
    TestExitCodePropagation();
    TestStdoutCapture();
    TestStderrCapture();
    TestBothStreams();
    TestTimeout();
    TestInfiniteOutputTimeout();
    TestTruncation();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "all process-runner tests passed\n";
    return EXIT_SUCCESS;
}