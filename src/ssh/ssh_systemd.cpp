#include "ssh_systemd.h"
#include "ssh_tunnel.h"
#include "ssh_process.h"
#include "ssh_arguments.h"
#include "ssh_auth.h"
#include <windows.h>
#include <winsvc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <iomanip>

#pragma comment(lib, "advapi32.lib")

using namespace ssh_tunnel;

// ------------------------------------------------------------
// Parsing helpers
// ------------------------------------------------------------

ServiceState ssh_tunnel::parse_service_state(const std::string& state_str) {
    std::string s = state_str;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s == "active") return ServiceState::ACTIVE;
    if (s == "inactive") return ServiceState::INACTIVE;
    if (s == "activating") return ServiceState::ACTIVATING;
    if (s == "deactivating") return ServiceState::DEACTIVATING;
    if (s == "failed") return ServiceState::FAILED;
    if (s == "reloading") return ServiceState::RELOADING;
    return ServiceState::UNKNOWN;
}

ServiceType ssh_tunnel::parse_service_type(const std::string& type_str) {
    std::string s = type_str;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s == "simple") return ServiceType::SIMPLE;
    if (s == "forking") return ServiceType::FORKING;
    if (s == "oneshot") return ServiceType::ONESHOT;
    if (s == "dbus") return ServiceType::DBUS;
    if (s == "notify") return ServiceType::NOTIFY;
    if (s == "idle") return ServiceType::IDLE;
    return ServiceType::UNKNOWN;
}

static std::chrono::system_clock::time_point parse_active_timestamp(const std::string& ts) {
    // Format: "Day YYYY-MM-DD HH:MM:SS UTC" or similar
    // For simplicity, return epoch if parsing fails
    if (ts.empty() || ts == "n/a") return {};
    // A full implementation would parse the timestamp
    return {};
}

// ------------------------------------------------------------
// Parse systemctl show --all output
// ------------------------------------------------------------

SystemdService ssh_tunnel::parse_systemctl_show(std::string_view output) {
    SystemdService svc;
    svc.raw_output = std::string(output);
    
    std::string content(output);
    std::istringstream iss(content);
    std::string line;
    
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        
        if (key == "Id") {
            svc.name = value;
        } else if (key == "Description") {
            svc.description = value;
        } else if (key == "ActiveState") {
            svc.state = parse_service_state(value);
        } else if (key == "SubState") {
            svc.sub_state = value;
        } else if (key == "LoadState") {
            svc.load_state = value;
        } else if (key == "Type") {
            svc.type = parse_service_type(value);
        } else if (key == "MainPID") {
            try { svc.main_pid = static_cast<uint32_t>(std::stoul(value)); } catch (...) {}
        } else if (key == "ActiveEnterTimestamp" || key == "ActiveEnterTimestampMonotonic") {
            svc.active_since = parse_active_timestamp(value);
        } else if (key == "ExecStart") {
            // Format: { path=/usr/bin/nginx argv[]=/usr/bin/nginx -g daemon off; ... }
            // Extract the command part
            size_t argv_pos = value.find("argv[]=");
            if (argv_pos != std::string::npos) {
                svc.exec_start = value.substr(argv_pos + 7);
            } else {
                svc.exec_start = value;
            }
        } else if (key == "User") {
            svc.user = value;
        } else if (key == "Group") {
            svc.group = value;
        } else if (key == "WorkingDirectory") {
            svc.working_directory = value;
        } else if (key == "Environment") {
            if (!value.empty()) {
                svc.environment.push_back(value);
            }
        } else if (key == "UnitFileState") {
            svc.enabled = (value == "enabled");
        } else if (key == "FragmentPath") {
            svc.unit_file_path = value;
        }
    }
    
    return svc;
}

std::vector<std::pair<std::string, ServiceState>> ssh_tunnel::parse_systemctl_list_units(std::string_view output) {
    std::vector<std::pair<std::string, ServiceState>> services;
    std::string content(output);
    std::istringstream iss(content);
    std::string line;
    
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        
        // Format: "nginx.service loaded active running"
        std::istringstream line_ss(line);
        std::string unit, load, active, sub;
        line_ss >> unit >> load >> active >> sub;
        
        if (!unit.empty()) {
            services.emplace_back(unit, parse_service_state(active));
        }
    }
    return services;
}

bool ssh_tunnel::parse_systemctl_is_enabled(std::string_view output) {
    std::string s(output);
    // Trim whitespace
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(0, 1);
    return s == "enabled";
}

// ------------------------------------------------------------
// Remote service management via SSH
// ------------------------------------------------------------

ServiceOperationResult ssh_tunnel::run_remote_systemctl(const TunnelConfig& tunnel_cfg,
                                                         const AuthContext& auth_ctx,
                                                         std::string_view unit_name,
                                                         std::string_view operation,
                                                         std::atomic<bool>* cancel_requested)
{
    ServiceOperationResult result;
    
    // Validate operation
    static const std::vector<std::string> valid_ops = {
        "start", "stop", "restart", "reload", 
        "enable", "disable", "status", "is-enabled",
        "is-active", "is-failed"
    };
    bool valid = false;
    for (const auto& op : valid_ops) {
        if (op == operation) { valid = true; break; }
    }
    if (!valid) {
        result.error_message = "Invalid operation: " + std::string(operation);
        result.exit_code = 1;
        return result;
    }
    
    // Build SSH command: ssh ... systemctl <operation> <unit>
    ArgBuilderFlags flags = ArgBuilderFlags::BATCH_MODE | ArgBuilderFlags::STRICT_KEYS;
    ArgBuilder builder(flags);
    builder.set_host(tunnel_cfg.ssh_host);
    builder.set_user(tunnel_cfg.ssh_user);
    builder.set_port(tunnel_cfg.ssh_port);
    
    if (!tunnel_cfg.identity_file.empty()) {
        builder.set_identity_file(tunnel_cfg.identity_file);
    }
    
    std::string cmd = "systemctl " + std::string(operation) + " " + std::string(unit_name);
    std::wstring w_cmd(cmd.begin(), cmd.end());
    builder.set_remote_command(w_cmd);
    
    auto args = builder.build();
    
    ProcessConfig proc_cfg;
    proc_cfg.executable = L"resources\\openssh\\ssh.exe";
    proc_cfg.arguments = args;
    proc_cfg.timeoutMs = 30000; // 30s for service operations
    proc_cfg.stdoutLimitBytes = 1024 * 1024;
    proc_cfg.stderrLimitBytes = 256 * 1024;
    
    ProcessResult proc_result = RunSshProcess(proc_cfg, cancel_requested);
    
    result.exit_code = static_cast<uint32_t>(proc_result.exitCode);
    result.success = (proc_result.exitCode == 0);
    
    if (!result.success) {
        result.error_message = proc_result.stderrData.empty() ? proc_result.stdoutData : proc_result.stderrData;
    }
    
    // For status/is-active, parse the new state
    if (operation == "status" || operation == "is-active") {
        if (!proc_result.stdoutData.empty()) {
            result.new_state = parse_service_state(proc_result.stdoutData);
        }
    }
    
    return result;
}

SystemdService ssh_tunnel::get_remote_service(const TunnelConfig& tunnel_cfg,
                                               const AuthContext& auth_ctx,
                                               std::string_view unit_name,
                                               std::atomic<bool>* cancel_requested)
{
    SystemdService svc;
    svc.name = std::string(unit_name);
    
    ArgBuilderFlags flags = ArgBuilderFlags::BATCH_MODE | ArgBuilderFlags::STRICT_KEYS;
    ArgBuilder builder(flags);
    builder.set_host(tunnel_cfg.ssh_host);
    builder.set_user(tunnel_cfg.ssh_user);
    builder.set_port(tunnel_cfg.ssh_port);
    
    if (!tunnel_cfg.identity_file.empty()) {
        builder.set_identity_file(tunnel_cfg.identity_file);
    }
    
    std::string cmd = "systemctl show --all " + std::string(unit_name);
    std::wstring w_cmd(cmd.begin(), cmd.end());
    builder.set_remote_command(w_cmd);
    
    auto args = builder.build();
    
    ProcessConfig proc_cfg;
    proc_cfg.executable = L"resources\\openssh\\ssh.exe";
    proc_cfg.arguments = args;
    proc_cfg.timeoutMs = 15000;
    proc_cfg.stdoutLimitBytes = 1024 * 1024;
    proc_cfg.stderrLimitBytes = 256 * 1024;
    
    ProcessResult proc_result = RunSshProcess(proc_cfg, cancel_requested);
    
    if (proc_result.exitCode == 0 && !proc_result.stdoutData.empty()) {
        svc = parse_systemctl_show(proc_result.stdoutData);
    } else {
        svc.state = ServiceState::UNKNOWN;
        svc.raw_output = proc_result.stderrData.empty() ? proc_result.stdoutData : proc_result.stderrData;
    }
    
    return svc;
}

std::vector<SystemdService> ssh_tunnel::list_remote_services(const TunnelConfig& tunnel_cfg,
                                                              const AuthContext& auth_ctx,
                                                              ServiceState filter_state,
                                                              std::atomic<bool>* cancel_requested)
{
    std::vector<SystemdService> services;
    
    ArgBuilderFlags flags = ArgBuilderFlags::BATCH_MODE | ArgBuilderFlags::STRICT_KEYS;
    ArgBuilder builder(flags);
    builder.set_host(tunnel_cfg.ssh_host);
    builder.set_user(tunnel_cfg.ssh_user);
    builder.set_port(tunnel_cfg.ssh_port);
    
    if (!tunnel_cfg.identity_file.empty()) {
        builder.set_identity_file(tunnel_cfg.identity_file);
    }
    
    std::string cmd = "systemctl list-units --type=service --all --no-legend";
    std::wstring w_cmd(cmd.begin(), cmd.end());
    builder.set_remote_command(w_cmd);
    
    auto args = builder.build();
    
    ProcessConfig proc_cfg;
    proc_cfg.executable = L"resources\\openssh\\ssh.exe";
    proc_cfg.arguments = args;
    proc_cfg.timeoutMs = 15000;
    proc_cfg.stdoutLimitBytes = 1024 * 1024;
    proc_cfg.stderrLimitBytes = 256 * 1024;
    
    ProcessResult proc_result = RunSshProcess(proc_cfg, cancel_requested);
    
    if (proc_result.exitCode == 0 && !proc_result.stdoutData.empty()) {
        auto unit_list = parse_systemctl_list_units(proc_result.stdoutData);
        
        for (const auto& [unit, state] : unit_list) {
            if (filter_state != ServiceState::UNKNOWN && state != filter_state) continue;
            
            // Get full details for each service
            SystemdService svc = get_remote_service(tunnel_cfg, auth_ctx, unit, cancel_requested);
            services.push_back(svc);
        }
    }
    
    return services;
}

bool ssh_tunnel::remote_service_exists(const TunnelConfig& tunnel_cfg,
                                        const AuthContext& auth_ctx,
                                        std::string_view unit_name,
                                        std::atomic<bool>* cancel_requested)
{
    auto result = run_remote_systemctl(tunnel_cfg, auth_ctx, unit_name, "status", cancel_requested);
    // Status returns 0 for active/inactive, 3 for not-found, 4 for dead
    return result.exit_code != 3 && result.exit_code != 4;
}

// ------------------------------------------------------------
// Local Windows Services (for cross-platform reference)
// ------------------------------------------------------------

std::vector<WindowsServiceInfo> ssh_tunnel::get_local_windows_services(std::string_view filter) {
    std::vector<WindowsServiceInfo> services;
    
    SC_HANDLE scm = OpenSCManager(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm) return services;
    
    DWORD bytes_needed = 0;
    DWORD services_returned = 0;
    DWORD resume_handle = 0;
    
    // First call to get buffer size
    EnumServicesStatusExA(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                          nullptr, 0, &bytes_needed, &services_returned, &resume_handle, nullptr);
    
    if (GetLastError() != ERROR_MORE_DATA) {
        CloseServiceHandle(scm);
        return services;
    }
    
    std::vector<BYTE> buffer(bytes_needed);
    ENUM_SERVICE_STATUS_PROCESSA* enum_data = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSA*>(buffer.data());
    
    if (EnumServicesStatusExA(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                              buffer.data(), bytes_needed, &bytes_needed, &services_returned, &resume_handle, nullptr)) {
        for (DWORD i = 0; i < services_returned; ++i) {
            const auto& s = enum_data[i];
            std::string name(s.lpServiceName);
            if (!filter.empty() && name.find(std::string(filter)) == std::string::npos) continue;
            
            WindowsServiceInfo info;
            info.name = name;
            info.display_name = s.lpDisplayName ? s.lpDisplayName : "";
            info.pid = s.ServiceStatusProcess.dwProcessId;
            
            switch (s.ServiceStatusProcess.dwCurrentState) {
                case SERVICE_RUNNING: info.state = "RUNNING"; break;
                case SERVICE_STOPPED: info.state = "STOPPED"; break;
                case SERVICE_PAUSED: info.state = "PAUSED"; break;
                case SERVICE_START_PENDING: info.state = "STARTING"; break;
                case SERVICE_STOP_PENDING: info.state = "STOPPING"; break;
                case SERVICE_CONTINUE_PENDING: info.state = "CONTINUING"; break;
                case SERVICE_PAUSE_PENDING: info.state = "PAUSING"; break;
                default: info.state = "UNKNOWN"; break;
            }
            
            services.push_back(info);
        }
    }
    
    CloseServiceHandle(scm);
    return services;
}
