#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ssh_tunnel {

enum class LinuxCommandKind : uint8_t {
    PortList,
    ServiceList,
    ServiceStatus,
    ServiceStart,
    ServiceStop,
    ServiceRestart,
    ProcessList,
    ProcessTerminate,
    ProcessKill,
    SshConfigCheck,
    SshVersion,
};

struct LinuxCommandResult {
    bool success = false;
    std::string output;
    std::string error;
    int exitCode = -1;
};

std::string BuildRemoteSshCommand(std::string_view sshUserHost, LinuxCommandKind kind, std::string_view params);
bool ValidateSshHostAlias(std::string_view alias);
bool ValidateSystemdUnitName(std::string_view unit);

} // namespace ssh_tunnel