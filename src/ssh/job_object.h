#pragma once

#include <windows.h>
#include <cstdint>
#include <memory>
#include <string>

namespace ssh_tunnel {

// ------------------------------------------------------------
// Windows Job Object wrapper for process-tree cleanup.
// Ensures all child processes are terminated when the job is closed.
// ------------------------------------------------------------

class JobObject {
public:
    JobObject();
    ~JobObject();

    JobObject(const JobObject&) = delete;
    JobObject& operator=(const JobObject&) = delete;
    JobObject(JobObject&& other) noexcept;
    JobObject& operator=(JobObject&& other) noexcept;

    // Create the job object with default security attributes.
    // Returns true on success.
    bool create(std::string_view name = "");

    // Assign a process to this job.
    // The process and all its descendants will be subject to job limits.
    // Returns true on success.
    bool assign_process(HANDLE process_handle);

    // Set active process limit to 1 (prevents child from creating new processes).
    // Returns true on success.
    bool set_active_process_limit(uint32_t limit = 1);

    // Set kill-on-job-close: when the job handle is closed, all processes in the job are terminated.
    // This is the key feature for guaranteed cleanup.
    // Returns true on success.
    bool set_kill_on_job_close(bool enable = true);

    // Terminate all processes in the job.
    // Returns true on success.
    bool terminate(uint32_t exit_code = 1u);

    // Check if job object is valid.
    bool is_valid() const noexcept { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }

    // Get raw handle (for advanced use).
    HANDLE handle() const noexcept { return handle_; }

    // Wait for all processes in the job to exit.
    // Returns WAIT_OBJECT_0 on success, WAIT_TIMEOUT on timeout, WAIT_FAILED on error.
    DWORD wait(DWORD timeout_ms = INFINITE) const;

private:
    HANDLE handle_ = nullptr;
};

// ------------------------------------------------------------
// Helper: Create a job object that kills all processes on close.
// Returns nullptr on failure.
// ------------------------------------------------------------

inline std::unique_ptr<JobObject> make_kill_on_close_job(std::string_view name = "") {
    auto job = std::make_unique<JobObject>();
    if (!job->create(name))
        return nullptr;
    if (!job->set_kill_on_job_close(true))
        return nullptr;
    return job;
}

} // namespace ssh_tunnel