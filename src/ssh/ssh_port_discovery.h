#pragma once

#include "ssh_tunnel.h"
#include "ssh_auth.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ssh_tunnel {

// ------------------------------------------------------------
// Port discovery via SSH remote command execution
// ------------------------------------------------------------

enum class TransportProtocol : uint8_t {
    UNKNOWN = 0,
    TCP     = 1,
    UDP     = 2,
};

struct ListeningPort {
    TransportProtocol protocol = TransportProtocol::UNKNOWN;
    std::string local_address;   // e.g. "0.0.0.0", "127.0.0.1", "::"
    uint16_t local_port = 0;
    std::string process_name;    // command name from ps/ss
    uint32_t pid = 0;
    std::string user;            // owning user
    std::string raw_line;        // original ss output line
};

// Parse output of: ss -H -ltnup (TCP listening) or ss -H -lunp (UDP listening)
// Returns vector of ListeningPort entries.
std::vector<ListeningPort> parse_ss_output(std::string_view ss_output,
                                           TransportProtocol filter = TransportProtocol::UNKNOWN);

// Run SSH command to execute remote port discovery.
// Uses the bundled ssh with BatchMode=yes and the given tunnel's SSH config.
// Returns parsed listening ports on success, empty on failure.
std::vector<ListeningPort> discover_remote_ports(const TunnelConfig& tunnel_cfg,
                                                  const AuthContext& auth_ctx,
                                                  std::atomic<bool>* cancel_requested = nullptr,
                                                  TransportProtocol filter = TransportProtocol::UNKNOWN);

// Convenience: discover TCP listening ports only
inline std::vector<ListeningPort> discover_remote_tcp_ports(const TunnelConfig& tunnel_cfg,
                                                             const AuthContext& auth_ctx,
                                                             std::atomic<bool>* cancel_requested = nullptr) {
    return discover_remote_ports(tunnel_cfg, auth_ctx, cancel_requested, TransportProtocol::TCP);
}

// Convenience: discover UDP listening ports only
inline std::vector<ListeningPort> discover_remote_udp_ports(const TunnelConfig& tunnel_cfg,
                                                             const AuthContext& auth_ctx,
                                                             std::atomic<bool>* cancel_requested = nullptr) {
    return discover_remote_ports(tunnel_cfg, auth_ctx, cancel_requested, TransportProtocol::UDP);
}

// ------------------------------------------------------------
// Local port discovery (Windows)
// ------------------------------------------------------------

struct LocalPortInfo {
    TransportProtocol protocol = TransportProtocol::UNKNOWN;
    std::string local_address;
    uint16_t local_port = 0;
    std::string process_name;
    uint32_t pid = 0;
};

// Get all local listening ports using GetExtendedTcpTable / GetExtendedUdpTable.
// Requires IPHLPAPI.
std::vector<LocalPortInfo> get_local_listening_ports(TransportProtocol filter = TransportProtocol::UNKNOWN);

// Check if a specific local port is in the listening table.
bool is_local_port_listening(uint16_t port, TransportProtocol protocol = TransportProtocol::TCP);

} // namespace ssh_tunnel