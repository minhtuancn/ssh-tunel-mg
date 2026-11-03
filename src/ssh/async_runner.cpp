#include "async_runner.h"
#include "ssh_process.h"
#include <algorithm>
#include <optional>
#include <unordered_map>

namespace ssh_tunnel {

AsyncRunner::AsyncRunner() {}

AsyncRunner::~AsyncRunner() {
    shutdown();
}

AsyncRunner::TaskHandle AsyncRunner::run_async(const ProcessConfig& config,
                                               CompletionCallback callback,
                                               bool use_job_object) {
    if (shutting_down_.load()) {
        return {0, nullptr};
    }

    auto task = std::make_unique<Task>();
    TaskHandle handle;
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        task->id = next_id_++;
        task->config = config;
        task->callback = std::move(callback);
        task->cancel_flag = std::make_shared<std::atomic<bool>>(false);
        handle.id = task->id;
        handle.cancel_flag = task->cancel_flag;

        if (use_job_object) {
            task->job = make_kill_on_close_job("SSHTunnelAsync_" + std::to_string(task->id));
        }

        tasks_.emplace(task->id, std::move(task));
    }

    // Launch the worker
    auto& task_ref = *tasks_[handle.id];
    task_ref.future = std::async(std::launch::async, [this, id = handle.id]() {
        auto it = tasks_.find(id);
        if (it == tasks_.end()) {
            return ProcessResult{};
        }
        Task& task = *it->second;

        // Create Job Object for this process if enabled
        std::unique_ptr<JobObject> local_job;
        if (task.job) {
            local_job = std::move(task.job);
        }

        // Run the process
        ProcessResult result = RunSshProcess(task.config, *task.cancel_flag);

        // If we have a Job Object, wait for it to be cleaned up
        if (local_job) {
            local_job->wait(5000);
        }

        // Store result
        {
            std::lock_guard<std::mutex> lock(task.result_mutex);
            task.result = result;
            task.completed = true;
        }

        // Invoke callback
        if (task.callback) {
            task.callback(result);
        }

        cv_.notify_all();
        return result;
    });

    return handle;
}

bool AsyncRunner::cancel(TaskHandle handle) {
    if (handle.id == 0 || !handle.cancel_flag) {
        return false;
    }

    std::lock_guard<std::mutex> lock(tasks_mutex_);
    auto it = tasks_.find(handle.id);
    if (it == tasks_.end()) {
        return false;
    }

    handle.cancel_flag->store(true, std::memory_order_release);
    return true;
}

std::optional<ProcessResult> AsyncRunner::wait_for(TaskHandle handle, uint32_t timeout_ms) {
    if (handle.id == 0) {
        return std::nullopt;
    }

    std::unique_lock<std::mutex> lock(tasks_mutex_);
    auto it = tasks_.find(handle.id);
    if (it == tasks_.end()) {
        return std::nullopt;
    }

    Task& task = *it->second;
    if (task.completed) {
        std::lock_guard<std::mutex> result_lock(task.result_mutex);
        return task.result;
    }

    // Wait for completion
    if (timeout_ms == 0) {
        cv_.wait(lock, [&task] { return task.completed; });
    } else {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        if (!cv_.wait_until(lock, deadline, [&task] { return task.completed; })) {
            return std::nullopt; // Timeout
        }
    }

    std::lock_guard<std::mutex> result_lock(task.result_mutex);
    return task.result;
}

void AsyncRunner::cancel_all() {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    for (auto& [id, task] : tasks_) {
        if (task->cancel_flag) {
            task->cancel_flag->store(true, std::memory_order_release);
        }
    }
}

bool AsyncRunner::wait_all(uint32_t timeout_ms) {
    std::unique_lock<std::mutex> lock(tasks_mutex_);

    if (timeout_ms == 0) {
        cv_.wait(lock, [this] {
            return std::all_of(tasks_.begin(), tasks_.end(),
                               [](const auto& p) { return p.second->completed; });
        });
        return true;
    } else {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        bool all_done = cv_.wait_until(lock, deadline, [this] {
            return std::all_of(tasks_.begin(), tasks_.end(),
                               [](const auto& p) { return p.second->completed; });
        });
        return all_done;
    }
}

size_t AsyncRunner::running_count() const noexcept {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    size_t count = 0;
    for (const auto& [id, task] : tasks_) {
        if (!task->completed) ++count;
    }
    return count;
}

void AsyncRunner::shutdown() {
    if (shutting_down_.exchange(true)) {
        return; // Already shutting down
    }

    cancel_all();
    wait_all(10000); // Wait up to 10 seconds

    // Clear completed tasks
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    tasks_.clear();
}

} // namespace ssh_tunnel