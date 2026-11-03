#pragma once

#include "ssh_tunnel.h"
#include "ssh_auth.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>

namespace ssh_tunnel {

// ------------------------------------------------------------
// Remote process management via SSH (ps, kill)
// ------------------------------------------------------------

enum class ProcessState : uint8_t {
    UNKNOWN = 0,
    RUNNING   = 1,
    SLEEPING  = 2,
    WAITING   = 3,
    ZOMBIE    = 4,
    STOPPED   = 5,
    TRACING   = 6,
    DEAD      = 7,
    WAKEKILL  = 8,
    WAKING    = 9,
    PARKED    = 10,
};

struct RemoteProcess {
    uint32_t pid = 0;
    std::string user;
    std::string command;
    std::string args;
    ProcessState state = ProcessState::UNKNOWN;
    double cpu_percent = 0.0;
    double mem_percent = 0.0;
    uint64_t vsz = 0;
    uint64_t rss = 0;
    std::string tty;
    std::chrono::system_clock::time_point start_time;
    std::string raw_line;
};

std::vector<RemoteProcess> parse_ps_output(std::string_view ps_output);

std::vector<RemoteProcess> list_remote_processes(const TunnelConfig& tunnel_cfg,
                                                  const AuthContext& auth_ctx,
                                                  std::atomic<bool>* cancel_requested = nullptr);

std::vector<RemoteProcess> find_remote_processes(const TunnelConfig& tunnel_cfg,
                                                  const AuthContext& auth_ctx,
                                                  std::string_view name_pattern,
                                                  std::atomic<bool>* cancel_requested = nullptr);

enum class KillSignal : int {
    TERM = 15,
    KILL = 9,
    INT  = 2,
    HUP  = 1,
    QUIT = 3,
    USR1 = 10,
    USR2 = 12,
};

struct KillResult {
    bool success = false;
    uint32_t exit_code = 0;
    std::string error_message;
};

KillResult kill_remote_process(const TunnelConfig& tunnel_cfg,
                               const AuthContext& auth_ctx,
                               uint32_t pid,
                               KillSignal signal = KillSignal::TERM,
                               std::atomic<bool>* cancel_requested = nullptr);

std::vector<KillResult> kill_remote_processes(const TunnelConfig& tunnel_cfg,
                                               const AuthContext& auth_ctx,
                                               const std::vector<uint32_t>& pids,
                                               KillSignal signal = KillSignal::TERM,
                                               std::atomic<bool>* cancel_requested = nullptr);

// ------------------------------------------------------------
// Local process management (Windows)
// ------------------------------------------------------------

struct LocalProcessInfo {
    uint32_t pid = 0;
    std::string name;
    std::string exe_path;
    uint64_t memory_usage = 0;
};

std::vector<LocalProcessInfo> get_local_processes(std::string_view filter = "");

bool kill_local_process(uint32_t pid, bool force = false);

} // namespace ssh_tunnel