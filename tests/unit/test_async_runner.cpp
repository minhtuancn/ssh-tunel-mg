#include "async_runner.h"
#include "ssh_process.h"
#include <cassert>
#include <atomic>
#include <chrono>
#include <thread>

using namespace ssh_tunnel;

void test_async_runner_basic() {
    AsyncRunner runner;
    
    ProcessConfig config;
    config.executable = L"C:\\Windows\\System32\\cmd.exe";
    config.arguments = {L"cmd.exe", L"/c", L"exit 0"};
    config.timeoutMs = 5000;
    
    std::atomic<bool> completed{false};
    ProcessResult result;
    
    auto handle = runner.run_async(config, [&](const ProcessResult& r) {
        result = r;
        completed = true;
    }, false); // Don't use job object for this simple test
    
    assert(handle.id != 0);
    
    // Wait for completion
    auto wait_result = runner.wait_for(handle, 10000);
    assert(wait_result.has_value());
    assert(wait_result->exitCode == 0);
    assert(completed);
}

void test_async_runner_cancel() {
    AsyncRunner runner;
    
    ProcessConfig config;
    config.executable = L"C:\\Windows\\System32\\cmd.exe";
    config.arguments = {L"cmd.exe", L"/c", L"timeout /t 10"}; // Long running
    config.timeoutMs = 0; // No timeout
    
    auto handle = runner.run_async(config, [](const ProcessResult&) {}, false);
    assert(handle.id != 0);
    
    // Cancel immediately
    assert(runner.cancel(handle) == true);
    
    // Wait for cancellation
    auto wait_result = runner.wait_for(handle, 5000);
    assert(wait_result.has_value());
    assert(wait_result->cancelled == true);
}

void test_async_runner_cancel_nonexistent() {
    AsyncRunner runner;
    
    AsyncRunner::TaskHandle handle;
    handle.id = 9999;
    handle.cancel_flag = std::make_shared<std::atomic<bool>>(false);
    
    assert(runner.cancel(handle) == false);
}

void test_async_runner_wait_all() {
    AsyncRunner runner;
    
    ProcessConfig config;
    config.executable = L"C:\\Windows\\System32\\cmd.exe";
    config.arguments = {L"cmd.exe", L"/c", L"exit 0"};
    config.timeoutMs = 5000;
    
    // Submit multiple tasks
    std::vector<AsyncRunner::TaskHandle> handles;
    for (int i = 0; i < 3; ++i) {
        auto h = runner.run_async(config, [](const ProcessResult&) {}, false);
        handles.push_back(h);
    }
    
    // Wait for all
    bool all_done = runner.wait_all(10000);
    assert(all_done);
    assert(runner.running_count() == 0);
}

void test_async_runner_shutdown() {
    AsyncRunner runner;
    
    ProcessConfig config;
    config.executable = L"C:\\Windows\\System32\\cmd.exe";
    config.arguments = {L"cmd.exe", L"/c", L"timeout /t 5"};
    config.timeoutMs = 0;
    
    runner.run_async(config, [](const ProcessResult&) {}, false);
    
    // Shutdown should cancel and wait
    runner.shutdown();
    assert(runner.running_count() == 0);
}

int main() {
    test_async_runner_basic();
    test_async_runner_cancel();
    test_async_runner_cancel_nonexistent();
    test_async_runner_wait_all();
    test_async_runner_shutdown();
    return 0;
}