#include "commands.h"

#include <cctype>

namespace ssh_tunnel {
namespace {

bool IsSafeSshTarget(std::string_view target) {
    if (target.empty() || target.front() == '-') return false;
    for (char c : target) {
        if (c <= ' ' || c == ';' || c == '&' || c == '|' || c == '>' ||
            c == '<' || c == '`' || c == '$' || c == '\'' || c == '"' ||
            c == '\\' || c == '(' || c == ')' || c == '\n' || c == '\r') {
            return false;
        }
    }
    return true;
}

bool IsSafeUnitName(std::string_view unit) {
    constexpr std::string_view suffix = ".service";
    if (unit.size() <= suffix.size()) return false;
    if (unit.front() == '-') return false;
    if (unit.substr(unit.size() - suffix.size()) != suffix) return false;
    for (char c : unit) {
        if (c <= ' ' || c == ';' || c == '&' || c == '|' || c == '>' ||
            c == '<' || c == '`' || c == '$' || c == '\'' || c == '"' ||
            c == '\\' || c == '(' || c == ')' || c == '/' || c == '*' ||
            c == '?' || c == '[' || c == ']' || c == '{' || c == '}' || c == '\n' || c == '\r') {
            return false;
        }
    }
    return true;
}

bool IsSafePid(std::string_view pid) {
    if (pid.size() < 2 || pid.size() > 7) return false; // pid 2..9999999
    for (char c : pid) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

} // namespace

bool ValidateSshHostAlias(std::string_view alias) {
    return IsSafeSshTarget(alias);
}

bool ValidateSystemdUnitName(std::string_view unit) {
    return IsSafeUnitName(unit);
}

std::string BuildRemoteSshCommand(std::string_view sshUserHost, LinuxCommandKind kind, std::string_view params) {
    if (!IsSafeSshTarget(sshUserHost)) return "";

    switch (kind) {
    case LinuxCommandKind::PortList:
        return "LC_ALL=C ss -H -ltnup";
    case LinuxCommandKind::ServiceList:
        return "LC_ALL=C systemctl list-units --type=service --all --no-legend --plain --no-pager";
    case LinuxCommandKind::ProcessList:
        return "LC_ALL=C ps -eo pid=,user=,comm=,args=";
    case LinuxCommandKind::ServiceStatus:
        if (!IsSafeUnitName(params)) return "";
        return "LC_ALL=C systemctl show --property=ActiveState,SubState,MainPID,Description " + std::string(params);
    case LinuxCommandKind::ServiceStart:
        if (!IsSafeUnitName(params)) return "";
        return "LC_ALL=C systemctl start " + std::string(params);
    case LinuxCommandKind::ServiceStop:
        if (!IsSafeUnitName(params)) return "";
        return "LC_ALL=C systemctl stop " + std::string(params);
    case LinuxCommandKind::ServiceRestart:
        if (!IsSafeUnitName(params)) return "";
        return "LC_ALL=C systemctl restart " + std::string(params);
    case LinuxCommandKind::ProcessTerminate:
        if (!IsSafePid(params)) return "";
        return "LC_ALL=C kill -TERM " + std::string(params);
    case LinuxCommandKind::ProcessKill:
        if (!IsSafePid(params)) return "";
        return "LC_ALL=C kill -KILL " + std::string(params);
    case LinuxCommandKind::SshConfigCheck:
        return "ssh -G 2>&1 | head -20";
    case LinuxCommandKind::SshVersion:
        return "ssh -V 2>&1";
    }
    return "";
}

} // namespace ssh_tunnel