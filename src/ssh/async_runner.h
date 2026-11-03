#pragma once

#include "ssh_process.h"
#include "job_object.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace ssh_tunnel {

// ------------------------------------------------------------
// AsyncRunner - runs SSH processes asynchronously with proper
// cancellation, timeout handling, and Job Object cleanup.
// ------------------------------------------------------------

class AsyncRunner {
public:
    using CompletionCallback = std::function<void(const ProcessResult&)>;

    AsyncRunner();
    ~AsyncRunner();

    AsyncRunner(const AsyncRunner&) = delete;
    AsyncRunner& operator=(const AsyncRunner&) = delete;

    // Submit a process to run asynchronously.
    // The callback is invoked on the runner's worker thread when the process completes.
    // Returns a handle that can be used to wait/cancel.
    struct TaskHandle {
        uint64_t id = 0;
        std::shared_ptr<std::atomic<bool>> cancel_flag;
    };

    // Run a process asynchronously.
    // If use_job_object is true, the process and its descendants are placed in a Job Object
    // that kills them all when the task is cancelled or the runner shuts down.
    TaskHandle run_async(const ProcessConfig& config,
                         CompletionCallback callback,
                         bool use_job_object = true);

    // Cancel a running task by its handle.
    // Returns true if the task was found and cancellation was requested.
    bool cancel(TaskHandle handle);

    // Wait for a specific task to complete.
    // Returns the result (or empty if task not found/cancelled).
    // If timeout_ms is 0, waits indefinitely.
    std::optional<ProcessResult> wait_for(TaskHandle handle, uint32_t timeout_ms = 0);

    // Cancel all running tasks.
    void cancel_all();

    // Wait for all tasks to complete.
    // If timeout_ms is 0, waits indefinitely.
    // Returns true if all completed, false if timeout.
    bool wait_all(uint32_t timeout_ms = 0);

    // Get number of currently running tasks.
    size_t running_count() const noexcept;

    // Shutdown the runner (cancels all tasks, waits for completion).
    void shutdown();

private:
    struct Task {
        uint64_t id;
        ProcessConfig config;
        CompletionCallback callback;
        std::shared_ptr<std::atomic<bool>> cancel_flag;
        std::unique_ptr<JobObject> job;
        std::future<ProcessResult> future;
        std::mutex result_mutex;
        std::optional<ProcessResult> result;
        bool completed = false;
    };

    mutable std::mutex tasks_mutex_;
    std::unordered_map<uint64_t, std::unique_ptr<Task>> tasks_;
    uint64_t next_id_ = 1;
    std::atomic<bool> shutting_down_{false};
    std::condition_variable cv_;
};

} // namespace ssh_tunnel