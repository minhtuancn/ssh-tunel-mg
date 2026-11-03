#include "job_object.h"
#include <windows.h>
#include <cstdint>

namespace ssh_tunnel {

JobObject::JobObject() : handle_(nullptr) {}

JobObject::~JobObject() {
    if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
}

JobObject::JobObject(JobObject&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
}

JobObject& JobObject::operator=(JobObject&& other) noexcept {
    if (this != &other) {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

bool JobObject::create(std::string_view name) {
    if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }

    // Create the job object
    LPCSTR job_name = nullptr;
    std::string name_str;
    if (!name.empty()) {
        name_str = std::string(name);
        job_name = name_str.c_str();
    }

    handle_ = CreateJobObjectW(nullptr, nullptr);
    if (!handle_ || handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    return true;
}

bool JobObject::assign_process(HANDLE process_handle) {
    if (!handle_ || handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }
    if (!process_handle || process_handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    return AssignProcessToJobObject(handle_, process_handle);
}

bool JobObject::set_active_process_limit(uint32_t limit) {
    if (!handle_ || handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    JOBOBJECT_BASIC_LIMIT_INFORMATION limits = {};
    limits.ActiveProcessLimit = limit;
    limits.LimitFlags = JOB_OBJECT_LIMIT_ACTIVE_PROCESS;

    if (!SetInformationJobObject(handle_, JobObjectBasicLimitInformation, &limits, sizeof(limits))) {
        return false;
    }
    return true;
}

bool JobObject::set_kill_on_job_close(bool enable) {
    if (!handle_ || handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (enable) {
        limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    } else {
        limits.BasicLimitInformation.LimitFlags &= ~JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    }

    if (!SetInformationJobObject(handle_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        return false;
    }
    return true;
}

bool JobObject::terminate(uint32_t exit_code) {
    if (!handle_ || handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }
    return TerminateJobObject(handle_, exit_code);
}

DWORD JobObject::wait(DWORD timeout_ms) const {
    if (!handle_ || handle_ == INVALID_HANDLE_VALUE) {
        return WAIT_FAILED;
    }
    return WaitForSingleObject(handle_, timeout_ms);
}

} // namespace ssh_tunnel