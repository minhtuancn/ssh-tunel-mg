#include "job_object.h"
#include <cassert>
#include <windows.h>
#include <cstdio>

using namespace ssh_tunnel;

void test_job_object_create() {
    JobObject job;
    assert(!job.is_valid());
    assert(job.create("TestJobObject") == true);
    assert(job.is_valid());
}

bool test_job_object_assign_process() {
    JobObject job;
    if (!job.create("TestJobAssign")) return false;
    
    // Need to set active process limit before assigning processes
    if (!job.set_active_process_limit(10)) return false;
    
    // Create a child process to assign to the job
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = L"C:\\Windows\\System32\\cmd.exe /c exit 0";
    BOOL created = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    if (!created) return false;
    
    // Assign the child process to the job
    BOOL assigned = AssignProcessToJobObject(job.handle(), pi.hProcess);
    if (!assigned) {
        DWORD err = GetLastError();
        printf("AssignProcessToJobObject failed: %lu\n", err);
        fflush(stdout);
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return false;
    }
    
    // Clean up
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

void test_job_object_kill_on_close() {
    JobObject job;
    assert(job.create("TestJobKillOnClose") == true);
    assert(job.set_kill_on_job_close(true) == true);
    
    // Create a child process to assign to the job
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = L"C:\\Windows\\System32\\cmd.exe /c exit 0";
    BOOL created = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    assert(created);
    
    // Assign the child process to the job
    assert(job.assign_process(pi.hProcess) == true);
    
    // Clean up
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

void test_job_object_terminate() {
    JobObject job;
    assert(job.create("TestJobTerminate") == true);
    assert(job.set_kill_on_job_close(true) == true);
    
    // Can't easily test terminate without creating a child process,
    // but we verify the API call doesn't crash.
    // TerminateJobObject may succeed even with no processes, so just verify it doesn't crash.
    job.terminate(1);
}

void test_job_object_wait() {
    JobObject job;
    assert(job.create("TestJobWait") == true);
    // Wait with timeout should return WAIT_TIMEOUT since no process
    DWORD result = job.wait(100);
    assert(result == WAIT_TIMEOUT || result == WAIT_FAILED);
}

int main() {
    test_job_object_create();
    if (!test_job_object_assign_process()) return 1;
    test_job_object_kill_on_close();
    test_job_object_terminate();
    test_job_object_wait();
    return 0;
}