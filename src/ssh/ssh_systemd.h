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
// systemd service management via SSH
// ------------------------------------------------------------

enum class ServiceState : uint8_t {
    UNKNOWN = 0,
    ACTIVE      = 1,
    INACTIVE    = 2,
    ACTIVATING  = 3,
    DEACTIVATING= 4,
    FAILED      = 5,
    RELOADING   = 6,
};

enum class ServiceType : uint8_t {
    UNKNOWN = 0,
    SIMPLE      = 1,
    FORKING     = 2,
    ONESHOT     = 3,
    DBUS        = 4,
    NOTIFY      = 5,
    IDLE        = 6,
};

struct SystemdService {
    std::string name;
    std::string description;
    ServiceState state = ServiceState::UNKNOWN;
    ServiceType type = ServiceType::UNKNOWN;
    std::string load_state;
    std::string sub_state;
    uint32_t main_pid = 0;
    std::chrono::system_clock::time_point active_since;
    std::string exec_start;
    std::string user;
    std::string group;
    std::string working_directory;
    std::vector<std::string> environment;
    bool enabled = false;
    std::string unit_file_path;
    std::string raw_output;
};

// Map systemctl state/type strings to enums.
ServiceState parse_service_state(const std::string& state_str);
ServiceType parse_service_type(const std::string& type_str);

// Parse output of: systemctl show --all --property=... <unit>
SystemdService parse_systemctl_show(std::string_view output);

// Parse output of: systemctl list-units --type=service --all --no-legend
std::vector<std::pair<std::string, ServiceState>> parse_systemctl_list_units(std::string_view output);

// Parse output of: systemctl is-enabled <unit>
bool parse_systemctl_is_enabled(std::string_view output);

// ------------------------------------------------------------
// Remote service management via SSH
// ------------------------------------------------------------

struct ServiceOperationResult {
    bool success = false;
    ServiceState new_state = ServiceState::UNKNOWN;
    std::string error_message;
    uint32_t exit_code = 0;
};

// Run systemctl command on remote host via SSH.
// Operations: start, stop, restart, reload, enable, disable, status, is-enabled
ServiceOperationResult run_remote_systemctl(const TunnelConfig& tunnel_cfg,
                                             const AuthContext& auth_ctx,
                                             std::string_view unit_name,
                                             std::string_view operation,
                                             std::atomic<bool>* cancel_requested = nullptr);

// Get full service details via systemctl show --all
SystemdService get_remote_service(const TunnelConfig& tunnel_cfg,
                                   const AuthContext& auth_ctx,
                                   std::string_view unit_name,
                                   std::atomic<bool>* cancel_requested = nullptr);

// List all services (or filtered by state)
std::vector<SystemdService> list_remote_services(const TunnelConfig& tunnel_cfg,
                                                  const AuthContext& auth_ctx,
                                                  ServiceState filter_state = ServiceState::UNKNOWN,
                                                  std::atomic<bool>* cancel_requested = nullptr);

// Check if a service exists on remote host
bool remote_service_exists(const TunnelConfig& tunnel_cfg,
                           const AuthContext& auth_ctx,
                           std::string_view unit_name,
                           std::atomic<bool>* cancel_requested = nullptr);

// ------------------------------------------------------------
// Local service management (Windows Services API)
// ------------------------------------------------------------

struct WindowsServiceInfo {
    std::string name;
    std::string display_name;
    std::string state;
    uint32_t pid = 0;
};

std::vector<WindowsServiceInfo> get_local_windows_services(std::string_view filter = "");

} // namespace ssh_tunnel