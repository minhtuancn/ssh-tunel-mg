#include "ssh_port_discovery.h"
#include "ssh_tunnel.h"
#include "ssh_process.h"
#include "ssh_arguments.h"
#include "ssh_auth.h"
// winsock2.h must be included before windows.h.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

// ------------------------------------------------------------
// Remote port discovery via SSH + ss command
// ------------------------------------------------------------

std::vector<ssh_tunnel::ListeningPort> ssh_tunnel::parse_ss_output(std::string_view ss_output,
                                           ssh_tunnel::TransportProtocol filter)
{
    std::vector<ssh_tunnel::ListeningPort> ports;
    
    std::string content(ss_output);
    std::istringstream iss(content);
    std::string line;
    
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        
        std::istringstream line_ss(line);
        std::string proto_str, state, recv_q, send_q, local_addr, peer_addr, users;
        
        line_ss >> proto_str >> state >> recv_q >> send_q >> local_addr >> peer_addr;
        std::getline(line_ss, users);
        
        ssh_tunnel::TransportProtocol proto = ssh_tunnel::TransportProtocol::UNKNOWN;
        if (proto_str == "tcp") proto = ssh_tunnel::TransportProtocol::TCP;
        else if (proto_str == "udp") proto = ssh_tunnel::TransportProtocol::UDP;
        
        if (filter != ssh_tunnel::TransportProtocol::UNKNOWN && proto != filter)
            continue;
        
        std::string addr = local_addr;
        uint16_t port = 0;
        size_t colon_pos = addr.rfind(':');
        if (colon_pos != std::string::npos) {
            std::string port_str = addr.substr(colon_pos + 1);
            try {
                port = static_cast<uint16_t>(std::stoi(port_str));
                addr = addr.substr(0, colon_pos);
            } catch (...) {
                port = 0;
            }
        }

        // ss prints IPv6 endpoints as [addr]:port; expose the bare address.
        if (addr.size() >= 2 && addr.front() == '[' && addr.back() == ']') {
            addr = addr.substr(1, addr.size() - 2);
        }
        
        uint32_t pid = 0;
        std::string proc_name;
        size_t users_pos = users.find("users:((");
        if (users_pos != std::string::npos) {
            size_t end_pos = users.find("))", users_pos);
            if (end_pos != std::string::npos) {
                std::string users_content = users.substr(users_pos + 8, end_pos - users_pos - 8);
                size_t comma1 = users_content.find(',');
                if (comma1 != std::string::npos) {
                    proc_name = users_content.substr(0, comma1);
                    if (proc_name.size() >= 2 && proc_name.front() == '"' && proc_name.back() == '"') {
                        proc_name = proc_name.substr(1, proc_name.size() - 2);
                    }
                    std::string rest = users_content.substr(comma1 + 1);
                    size_t pid_pos = rest.find("pid=");
                    if (pid_pos != std::string::npos) {
                        std::string pid_str = rest.substr(pid_pos + 4);
                        size_t comma2 = pid_str.find(',');
                        if (comma2 != std::string::npos) {
                            pid_str = pid_str.substr(0, comma2);
                        }
                        try {
                            pid = static_cast<uint32_t>(std::stoul(pid_str));
                        } catch (...) {}
                    }
                }
            }
        }
        
        ssh_tunnel::ListeningPort lp;
        lp.protocol = proto;
        lp.local_address = addr;
        lp.local_port = port;
        lp.process_name = proc_name;
        lp.pid = pid;
        lp.user = "";
        lp.raw_line = line;
        
        if (port > 0) {
            ports.push_back(lp);
        }
    }
    
    return ports;
}

std::vector<ssh_tunnel::ListeningPort> ssh_tunnel::discover_remote_ports(const ssh_tunnel::TunnelConfig& tunnel_cfg,
                                                  const ssh_tunnel::AuthContext& auth_ctx,
                                                  std::atomic<bool>* cancel_requested,
                                                  ssh_tunnel::TransportProtocol filter)
{
    std::vector<ssh_tunnel::ListeningPort> result;
    
    ssh_tunnel::ArgBuilderFlags flags = ssh_tunnel::ArgBuilderFlags::BATCH_MODE | ssh_tunnel::ArgBuilderFlags::STRICT_KEYS;
    ssh_tunnel::ArgBuilder builder(flags);
    builder.set_host(tunnel_cfg.ssh_host);
    builder.set_user(tunnel_cfg.ssh_user);
    builder.set_port(tunnel_cfg.ssh_port);
    
    if (!tunnel_cfg.identity_file.empty()) {
        builder.set_identity_file(tunnel_cfg.identity_file);
    }
    
    std::string ss_cmd = "ss -H -l";
    if (filter == ssh_tunnel::TransportProtocol::TCP || filter == ssh_tunnel::TransportProtocol::UNKNOWN) {
        ss_cmd += "t";
    }
    if (filter == ssh_tunnel::TransportProtocol::UDP || filter == ssh_tunnel::TransportProtocol::UNKNOWN) {
        ss_cmd += "u";
    }
    ss_cmd += "np";
    
    // Convert to wstring for set_remote_command
    std::wstring w_ss_cmd(ss_cmd.begin(), ss_cmd.end());
    builder.set_remote_command(w_ss_cmd);
    
    auto args = builder.build();
    
    ssh_tunnel::ProcessConfig proc_cfg;
    proc_cfg.executable = L"resources\\openssh\\ssh.exe";
    proc_cfg.arguments = args;
    proc_cfg.timeoutMs = 15000;
    proc_cfg.stdoutLimitBytes = 1024 * 1024;
    proc_cfg.stderrLimitBytes = 256 * 1024;
    
    ssh_tunnel::ProcessResult proc_result = ssh_tunnel::RunSshProcess(proc_cfg, cancel_requested);
    
    if (proc_result.exitCode == 0 && !proc_result.stdoutData.empty()) {
        result = ssh_tunnel::parse_ss_output(proc_result.stdoutData, filter);
    }
    
    return result;
}

// ------------------------------------------------------------
// Local port discovery (Windows GetExtendedTcpTable/UdpTable)
// ------------------------------------------------------------

std::vector<ssh_tunnel::LocalPortInfo> ssh_tunnel::get_local_listening_ports(ssh_tunnel::TransportProtocol filter)
{
    std::vector<ssh_tunnel::LocalPortInfo> ports;
    
    if (filter == ssh_tunnel::TransportProtocol::TCP || filter == ssh_tunnel::TransportProtocol::UNKNOWN) {
        DWORD size = 0;
        GetExtendedTcpTable(nullptr, &size, TRUE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
        
        if (size > 0) {
            std::vector<BYTE> buffer(size);
            PMIB_TCPTABLE_OWNER_PID table = reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(buffer.data());
            
            if (GetExtendedTcpTable(table, &size, TRUE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) == NO_ERROR) {
                for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                    const auto& row = table->table[i];
                    if (row.dwState == MIB_TCP_STATE_LISTEN) {
                        ssh_tunnel::LocalPortInfo info;
                        info.protocol = ssh_tunnel::TransportProtocol::TCP;
                        info.local_port = ntohs(static_cast<u_short>(row.dwLocalPort));
                        info.pid = row.dwOwningPid;
                        
                        in_addr addr;
                        addr.S_un.S_addr = row.dwLocalAddr;
                        char ip_str[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str));
                        info.local_address = ip_str;
                        info.process_name = "";
                        
                        ports.push_back(info);
                    }
                }
            }
        }
    }
    
    if (filter == ssh_tunnel::TransportProtocol::UDP || filter == ssh_tunnel::TransportProtocol::UNKNOWN) {
        DWORD size = 0;
        GetExtendedUdpTable(nullptr, &size, TRUE, AF_INET, UDP_TABLE_OWNER_PID, 0);
        
        if (size > 0) {
            std::vector<BYTE> buffer(size);
            PMIB_UDPTABLE_OWNER_PID table = reinterpret_cast<PMIB_UDPTABLE_OWNER_PID>(buffer.data());
            
            if (GetExtendedUdpTable(table, &size, TRUE, AF_INET, UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
                for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                    const auto& row = table->table[i];
                    ssh_tunnel::LocalPortInfo info;
                    info.protocol = ssh_tunnel::TransportProtocol::UDP;
                    info.local_port = ntohs(static_cast<u_short>(row.dwLocalPort));
                    info.pid = row.dwOwningPid;
                    
                    in_addr addr;
                    addr.S_un.S_addr = row.dwLocalAddr;
                    char ip_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str));
                    info.local_address = ip_str;
                    info.process_name = "";
                    
                    ports.push_back(info);
                }
            }
        }
    }
    
    return ports;
}

bool ssh_tunnel::is_local_port_listening(uint16_t port, ssh_tunnel::TransportProtocol protocol)
{
    auto ports = ssh_tunnel::get_local_listening_ports(protocol);
    for (const auto& p : ports) {
        if (p.local_port == port) return true;
    }
    return false;
}