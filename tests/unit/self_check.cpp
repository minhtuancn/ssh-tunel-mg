#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "commands.h"

bool ParsePort(std::wstring_view input, uint16_t& out);
bool ParsePid(std::wstring_view input, uint32_t& out);
bool ValidateHost(std::wstring_view input);
bool ValidateUnit(std::wstring_view input);
std::wstring BuildWindowsCommandLineArgument(std::wstring_view argument);

namespace {
int failures = 0;

void Check(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAIL: " << name << '\n';
        ++failures;
    }
}

void TestPorts() {
    uint16_t port = 0;
    Check(ParsePort(L"1", port) && port == 1, "port lower bound");
    Check(ParsePort(L"65535", port) && port == 65535, "port upper bound");
    Check(!ParsePort(L"0", port), "reject port zero");
    Check(!ParsePort(L"65536", port), "reject port overflow");
    Check(!ParsePort(L"12x", port), "reject nonnumeric port");
    Check(!ParsePort(L"", port), "reject empty port");
}

void TestPids() {
    uint32_t pid = 0;
    Check(ParsePid(L"1", pid) && pid == 1, "parse positive pid");
    Check(!ParsePid(L"0", pid), "reject zero pid");
    Check(!ParsePid(L"-2", pid), "reject negative pid");
    Check(!ParsePid(L"4294967296", pid), "reject pid overflow");
}

void TestWindowsArgumentQuoting() {
    Check(BuildWindowsCommandLineArgument(L"plain") == L"plain", "leave simple Windows argument unquoted");
    Check(BuildWindowsCommandLineArgument(L"path with spaces\\") == L"\"path with spaces\\\\\"", "quote spaces and trailing backslash");
    Check(BuildWindowsCommandLineArgument(L"a\"b") == L"\"a\\\"b\"", "escape embedded quote");
    Check(BuildWindowsCommandLineArgument(L"") == L"\"\"", "quote empty argument");
}

void TestHostAndUnit() {
    Check(ValidateHost(L"dev.example.net"), "accept hostname");
    Check(ValidateHost(L"10.20.10.103"), "accept IPv4");
    Check(ValidateHost(L"2001:db8::1"), "accept IPv6 literal");
    Check(ValidateHost(L"db-prod"), "accept ssh alias");
    Check(!ValidateHost(L"-oProxyCommand=bad"), "reject leading option host");
    Check(!ValidateHost(L"host;id"), "reject shell metacharacter host");
    Check(!ValidateHost(L"host\ncommand"), "reject newline host");

    Check(ValidateUnit(L"nginx.service"), "accept service unit");
    Check(!ValidateUnit(L"-evil.service"), "reject option-like unit");
    Check(!ValidateUnit(L"nginx;id.service"), "reject shell metacharacter unit");
    Check(!ValidateUnit(L"nginx"), "require service suffix");
}

void TestLinuxCommandBuilder() {
    using namespace ssh_tunnel;
    Check(BuildRemoteSshCommand("dev@10.20.10.103", LinuxCommandKind::PortList, "") == "LC_ALL=C ss -H -ltnup", "port list fixed command");
    Check(BuildRemoteSshCommand("dev@host", LinuxCommandKind::ServiceList, "") == "LC_ALL=C systemctl list-units --type=service --all --no-legend --plain --no-pager", "service list fixed command");
    Check(BuildRemoteSshCommand("dev@host", LinuxCommandKind::ServiceStart, "nginx.service") == "LC_ALL=C systemctl start nginx.service", "service start fixed command");
    Check(BuildRemoteSshCommand("dev@host", LinuxCommandKind::ServiceStop, "nginx.service") == "LC_ALL=C systemctl stop nginx.service", "service stop fixed command");
    Check(BuildRemoteSshCommand("dev@host", LinuxCommandKind::ServiceRestart, "nginx.service") == "LC_ALL=C systemctl restart nginx.service", "service restart fixed command");
    Check(BuildRemoteSshCommand("dev@host", LinuxCommandKind::ServiceStart, "-oProxyCommand=bad") == "", "reject option injection unit");
    Check(BuildRemoteSshCommand("dev@host", LinuxCommandKind::ServiceStart, "nginx;id.service") == "", "reject shell metacharacter unit");
    Check(BuildRemoteSshCommand("dev@host", LinuxCommandKind::ServiceStart, "nginx") == "", "reject unit without suffix");
    Check(BuildRemoteSshCommand("-oProxyCommand=bad", LinuxCommandKind::PortList, "") == "", "reject option-injection ssh target");
    Check(BuildRemoteSshCommand("dev@host", LinuxCommandKind::ProcessTerminate, "1234") == "LC_ALL=C kill -TERM 1234", "process terminate fixed command");
    Check(BuildRemoteSshCommand("dev@host", LinuxCommandKind::ProcessKill, "1234") == "LC_ALL=C kill -KILL 1234", "process kill fixed command");
    Check(BuildRemoteSshCommand("dev@host", LinuxCommandKind::ProcessTerminate, "1") == "", "reject pid 1");
    Check(BuildRemoteSshCommand("dev@host", LinuxCommandKind::ProcessKill, "0") == "", "reject pid 0");
    Check(BuildRemoteSshCommand("dev@host", LinuxCommandKind::ProcessKill, "-1") == "", "reject negative pid");
    Check(BuildRemoteSshCommand("dev@host", LinuxCommandKind::ProcessKill, "1;reboot") == "", "reject non-numeric pid");
    Check(BuildRemoteSshCommand("dev@host", LinuxCommandKind::ProcessList, "") == "LC_ALL=C ps -eo pid=,user=,comm=,args=", "process list fixed command");
    Check(ValidateSshHostAlias("dev@10.20.10.103"), "accept user@host");
    Check(ValidateSshHostAlias("db-primary"), "accept plain alias");
    Check(!ValidateSshHostAlias("-oProxyCommand=bad"), "reject option injection alias");
    Check(!ValidateSshHostAlias("dev@host;id"), "reject semicolon in target");
    Check(!ValidateSshHostAlias(""), "reject empty target");
}
}

int main() {
    TestPorts();
    TestPids();
    TestWindowsArgumentQuoting();
    TestHostAndUnit();
    TestLinuxCommandBuilder();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "all self-checks passed\n";
    return EXIT_SUCCESS;
}
