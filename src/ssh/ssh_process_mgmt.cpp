#include "ssh_process_mgmt.h"
#include "ssh_tunnel.h"
#include "ssh_process.h"
#include "ssh_arguments.h"
#include "ssh_auth.h"
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <chrono>

#pragma comment(lib, "psapi.lib")

using namespace ssh_tunnel;

// ------------------------------------------------------------
// Parsing helpers
// ------------------------------------------------------------

static ProcessState parse_process_state(const std::string& stat) {
    if (stat.empty()) return ProcessState::UNKNOWN;
    char c = stat[0];
    switch (c) {
        case 'R': return ProcessState::RUNNING;
        case 'S': return ProcessState::SLEEPING;
        case 'D': return ProcessState::WAITING;
        case 'Z': return ProcessState::ZOMBIE;
        case 'T': return ProcessState::STOPPED;
        case 't': return ProcessState::TRACING;
        case 'X': return ProcessState::DEAD;
        case 'K': return ProcessState::WAKEKILL;
        case 'W': return ProcessState::WAKING;
        case 'P': return ProcessState::PARKED;
        default: return ProcessState::UNKNOWN;
    }
}

static std::chrono::system_clock::time_point parse_start_time(const std::string& ts) {
    // Format: "Mon Oct 25 10:30:00 2026"
    // Simplified - return epoch for now
    return {};
}

// ------------------------------------------------------------
// Parse ps -eo output
// ------------------------------------------------------------

std::vector<RemoteProcess> ssh_tunnel::parse_ps_output(std::string_view ps_output) {
    std::vector<RemoteProcess> processes;
    std::string content(ps_output);
    std::istringstream iss(content);
    std::string line;
    
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        
        // ps -eo pid=,user=,comm=,args=,stat=,%cpu=,%mem=,vsz=,rss=,tty=,lstart=
        // Fields separated by spaces, but args may contain spaces
        // We need to parse carefully
        
        std::istringstream line_ss(line);
        RemoteProcess proc;
        proc.raw_line = line;
        
        // pid
        if (!(line_ss >> proc.pid)) continue;
        
        // user
        line_ss >> proc.user;
        
        // comm (command name)
        line_ss >> proc.command;
        
        // args - read rest of line but we need to split stat, %cpu, %mem, vsz, rss, tty, lstart
        // Strategy: read remaining into string, then parse from end
        std::string remainder;
        std::getline(line_ss, remainder);
        
        // Trim leading space
        while (!remainder.empty() && std::isspace(static_cast<unsigned char>(remainder.front())))
            remainder.erase(0, 1);
        
        // Parse from end: lstart, tty, rss, vsz, %mem, %cpu, stat, then args is everything before stat
        // This is complex; simplified approach: split by spaces and take known positions from end
        
        std::vector<std::string> tokens;
        std::istringstream rem_ss(remainder);
        std::string token;
        while (rem_ss >> token) tokens.push_back(token);
        
        if (tokens.size() < 7) continue; // stat, %cpu, %mem, vsz, rss, tty minimum

        // ps lstart is "Mon Oct 25 10:30:00 2026" (5 tokens), so with lstart:
        //   stat, %cpu, %mem, vsz, rss, tty, <lstart x5>
        // Without lstart the layout ends at tty.
        auto is_state_token = [](const std::string& t) {
            return t.size() == 1 && parse_process_state(t) != ProcessState::UNKNOWN;
        };

        size_t stat_idx = tokens.size() - 7;
        if (tokens.size() >= 11 && is_state_token(tokens[tokens.size() - 11])) {
            stat_idx = tokens.size() - 11;
        } else if (!is_state_token(tokens[stat_idx])) {
            continue;
        }
        proc.state = parse_process_state(tokens[stat_idx]);
        
        try { proc.cpu_percent = std::stod(tokens[stat_idx + 1]); } catch (...) {}
        try { proc.mem_percent = std::stod(tokens[stat_idx + 2]); } catch (...) {}
        try { proc.vsz = std::stoull(tokens[stat_idx + 3]); } catch (...) {}
        try { proc.rss = std::stoull(tokens[stat_idx + 4]); } catch (...) {}
        proc.tty = tokens[stat_idx + 5];
        
        // lstart = remaining tokens
        std::string lstart_str;
        for (size_t i = stat_idx + 6; i < tokens.size(); ++i) {
            if (i > stat_idx + 6) lstart_str += " ";
            lstart_str += tokens[i];
        }
        proc.start_time = parse_start_time(lstart_str);
        
        // args = join tokens[0..stat_idx-1]
        for (size_t i = 0; i < stat_idx; ++i) {
            if (i > 0) proc.args += " ";
            proc.args += tokens[i];
        }
        
        processes.push_back(proc);
    }
    
    return processes;
}

// ------------------------------------------------------------
// Remote process listing via SSH
// ------------------------------------------------------------

std::vector<RemoteProcess> ssh_tunnel::list_remote_processes(const TunnelConfig& tunnel_cfg,
                                                              const AuthContext& auth_ctx,
                                                              std::atomic<bool>* cancel_requested) {
    std::vector<RemoteProcess> result;
    
    ArgBuilderFlags flags = ArgBuilderFlags::BATCH_MODE | ArgBuilderFlags::STRICT_KEYS;
    ArgBuilder builder(flags);
    builder.set_host(tunnel_cfg.ssh_host);
    builder.set_user(tunnel_cfg.ssh_user);
    builder.set_port(tunnel_cfg.ssh_port);
    
    if (!tunnel_cfg.identity_file.empty()) {
        builder.set_identity_file(tunnel_cfg.identity_file);
    }
    
    std::string ps_cmd = "ps -eo pid=,user=,comm=,args=,stat=,%cpu=,%mem=,vsz=,rss=,tty=,lstart=";
    std::wstring w_cmd(ps_cmd.begin(), ps_cmd.end());
    builder.set_remote_command(w_cmd);
    
    auto args = builder.build();
    
    ProcessConfig proc_cfg;
    proc_cfg.executable = L"resources\\openssh\\ssh.exe";
    proc_cfg.arguments = args;
    proc_cfg.timeoutMs = 15000;
    proc_cfg.stdoutLimitBytes = 4 * 1024 * 1024;
    proc_cfg.stderrLimitBytes = 256 * 1024;
    
    ProcessResult proc_result = RunSshProcess(proc_cfg, cancel_requested);
    
    if (proc_result.exitCode == 0 && !proc_result.stdoutData.empty()) {
        result = parse_ps_output(proc_result.stdoutData);
    }
    
    return result;
}

std::vector<RemoteProcess> ssh_tunnel::find_remote_processes(const TunnelConfig& tunnel_cfg,
                                                              const AuthContext& auth_ctx,
                                                              std::string_view name_pattern,
                                                              std::atomic<bool>* cancel_requested) {
    auto all = list_remote_processes(tunnel_cfg, auth_ctx, cancel_requested);
    std::vector<RemoteProcess> matches;
    std::string pattern(name_pattern);
    std::transform(pattern.begin(), pattern.end(), pattern.begin(), ::tolower);
    
    for (const auto& p : all) {
        std::string cmd = p.command;
        std::string args = p.args;
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);
        std::transform(args.begin(), args.end(), args.begin(), ::tolower);
        
        if (cmd.find(pattern) != std::string::npos || args.find(pattern) != std::string::npos) {
            matches.push_back(p);
        }
    }
    return matches;
}

// ------------------------------------------------------------
// Kill remote process via SSH
// ------------------------------------------------------------

KillResult ssh_tunnel::kill_remote_process(const TunnelConfig& tunnel_cfg,
                                            const AuthContext& auth_ctx,
                                            uint32_t pid,
                                            KillSignal signal,
                                            std::atomic<bool>* cancel_requested) {
    KillResult result;
    
    ArgBuilderFlags flags = ArgBuilderFlags::BATCH_MODE | ArgBuilderFlags::STRICT_KEYS;
    ArgBuilder builder(flags);
    builder.set_host(tunnel_cfg.ssh_host);
    builder.set_user(tunnel_cfg.ssh_user);
    builder.set_port(tunnel_cfg.ssh_port);
    
    if (!tunnel_cfg.identity_file.empty()) {
        builder.set_identity_file(tunnel_cfg.identity_file);
    }
    
    std::string cmd = "kill -" + std::to_string(static_cast<int>(signal)) + " " + std::to_string(pid);
    std::wstring w_cmd(cmd.begin(), cmd.end());
    builder.set_remote_command(w_cmd);
    
    auto args = builder.build();
    
    ProcessConfig proc_cfg;
    proc_cfg.executable = L"resources\\openssh\\ssh.exe";
    proc_cfg.arguments = args;
    proc_cfg.timeoutMs = 10000;
    proc_cfg.stdoutLimitBytes = 256 * 1024;
    proc_cfg.stderrLimitBytes = 256 * 1024;
    
    ProcessResult proc_result = RunSshProcess(proc_cfg, cancel_requested);
    
    result.exit_code = static_cast<uint32_t>(proc_result.exitCode);
    result.success = (proc_result.exitCode == 0);
    
    if (!result.success) {
        result.error_message = proc_result.stderrData.empty() ? proc_result.stdoutData : proc_result.stderrData;
    }
    
    return result;
}

std::vector<KillResult> ssh_tunnel::kill_remote_processes(const TunnelConfig& tunnel_cfg,
                                                           const AuthContext& auth_ctx,
                                                           const std::vector<uint32_t>& pids,
                                                           KillSignal signal,
                                                           std::atomic<bool>* cancel_requested) {
    std::vector<KillResult> results;
    results.reserve(pids.size());
    
    for (uint32_t pid : pids) {
        results.push_back(kill_remote_process(tunnel_cfg, auth_ctx, pid, signal, cancel_requested));
    }
    return results;
}

// ------------------------------------------------------------
// Local Windows process management
// ------------------------------------------------------------

std::vector<LocalProcessInfo> ssh_tunnel::get_local_processes(std::string_view filter) {
    std::vector<LocalProcessInfo> processes;
    
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return processes;
    
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    
    if (Process32FirstW(snapshot, &entry)) {
        do {
            std::string name;
            // Convert from wide string
            int len = WideCharToMultiByte(CP_UTF8, 0, entry.szExeFile, -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                std::string n(len, '\0');
                WideCharToMultiByte(CP_UTF8, 0, entry.szExeFile, -1, &n[0], len, nullptr, nullptr);
                if (!n.empty() && n.back() == '\0') n.pop_back();
                name = n;
            }
            if (!filter.empty() && name.find(std::string(filter)) == std::string::npos) continue;
            
            LocalProcessInfo info;
            info.pid = entry.th32ProcessID;
            info.name = name;
            
            // Get executable path
            HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (proc) {
                wchar_t path[MAX_PATH];
                DWORD size = MAX_PATH;
                if (QueryFullProcessImageNameW(proc, 0, path, &size)) {
                    info.exe_path = std::string(path, path + size);
                } else {
                    info.exe_path = "";
                }
                // Get memory usage
                PROCESS_MEMORY_COUNTERS pmc;
                if (GetProcessMemoryInfo(proc, &pmc, sizeof(pmc))) {
                    info.memory_usage = pmc.WorkingSetSize;
                }
                CloseHandle(proc);
            }
            
            processes.push_back(info);
        } while (Process32NextW(snapshot, &entry));
    }
    
    CloseHandle(snapshot);
    return processes;
}

bool ssh_tunnel::kill_local_process(uint32_t pid, bool force) {
    HANDLE proc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!proc) return false;
    
    BOOL result = TerminateProcess(proc, force ? 1 : 0);
    CloseHandle(proc);
    return result == TRUE;
}