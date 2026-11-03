#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <memory>

namespace ssh_tunnel {

// ------------------------------------------------------------
// Tunnel configuration and state
// ------------------------------------------------------------

enum class TunnelStatus : uint32_t {
    STOPPED = 0,
    STARTING,
    RUNNING,
    STOPPING,
    ERROR_STATE,  // Avoid Windows ERROR macro conflict
};

enum class TunnelError : uint32_t {
    NONE = 0,
    PORT_IN_USE,
    SSH_PROCESS_FAILED,
    SSH_AUTH_FAILED,
    HOST_KEY_UNKNOWN,
    HOST_KEY_CHANGED,
    TIMEOUT,
    CANCELLED,
    INVALID_CONFIG,
    PROCESS_CRASHED,
};

struct TunnelConfig {
    // Local bind
    std::wstring local_host;       // e.g. "127.0.0.1" or "0.0.0.0"
    uint16_t local_port = 0;       // 0 = auto-assign (ephemeral)
    
    // Remote target (through SSH server)
    std::wstring remote_host;      // e.g. "127.0.0.1" or "db.internal"
    uint16_t remote_port = 0;
    
    // SSH connection
    std::wstring ssh_host;         // SSH server hostname/IP
    uint16_t ssh_port = 22;
    std::wstring ssh_user;
    std::wstring identity_file;    // optional
    bool use_agent = true;
    
    // Options
    bool enable_compression = false;
    uint32_t keepalive_interval_sec = 30;
    uint32_t connect_timeout_sec = 15;
};

struct TunnelInfo {
    TunnelStatus status = TunnelStatus::STOPPED;
    TunnelError last_error = TunnelError::NONE;
    std::wstring error_message;
    
    // Actual bound port (may differ from config.local_port if 0)
    uint16_t bound_local_port = 0;
    
    // Process info
    uint32_t ssh_pid = 0;
    std::chrono::steady_clock::time_point started_at;
    std::chrono::steady_clock::time_point last_keepalive;
    
    // Stats
    uint64_t bytes_sent = 0;
    uint64_t bytes_received = 0;
};

// ------------------------------------------------------------
// Tunnel manager - owns the SSH process for a single tunnel
// ------------------------------------------------------------

class Tunnel {
public:
    explicit Tunnel(const TunnelConfig& cfg);
    ~Tunnel();
    
    // Non-copyable, movable
    Tunnel(const Tunnel&) = delete;
    Tunnel& operator=(const Tunnel&) = delete;
    Tunnel(Tunnel&&) noexcept;
    Tunnel& operator=(Tunnel&&) noexcept;
    
    // Start the tunnel (launches SSH process with -L local:remote)
    // Returns immediately; tunnel runs in background thread.
    // Use wait_for_status() or poll get_info() to track progress.
    void start(std::atomic<bool>* cancel_requested = nullptr);
    
    // Request graceful stop (sends SIGTERM to SSH process)
    void stop();
    
    // Force kill (TerminateProcess)
    void kill();
    
    // Get current tunnel info (thread-safe)
    TunnelInfo get_info() const noexcept;
    
    // Wait for status change or timeout.
    // Returns current status after wait.
    TunnelStatus wait_for_status(TunnelStatus target, 
                                  uint32_t timeout_ms = 5000);
    
    // Check if tunnel is running and healthy
    bool is_healthy() const noexcept;
    
    // Get the SSH command line that would be executed (for debugging)
    std::vector<std::wstring> get_ssh_command_line() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

// ------------------------------------------------------------
// Multi-tunnel manager (for UI/dashboard)
// ------------------------------------------------------------

class TunnelManager {
public:
    TunnelManager();
    ~TunnelManager();
    
    TunnelManager(const TunnelManager&) = delete;
    TunnelManager& operator=(const TunnelManager&) = delete;
    
    // Add a tunnel configuration, returns tunnel ID (index)
    size_t add_tunnel(const TunnelConfig& cfg);
    
    // Start tunnel by ID
    bool start_tunnel(size_t id, std::atomic<bool>* cancel_requested = nullptr);
    
    // Stop tunnel by ID
    bool stop_tunnel(size_t id);
    
    // Remove tunnel (must be stopped first)
    bool remove_tunnel(size_t id);
    
    // Get tunnel info by ID
    TunnelInfo get_tunnel_info(size_t id) const;
    
    // Get all tunnel infos
    std::vector<TunnelInfo> get_all_tunnel_infos() const;
    
    // Count of tunnels
    size_t count() const noexcept;
    
    // Count of running tunnels
    size_t running_count() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

// ------------------------------------------------------------
// Port availability checking
// ------------------------------------------------------------

// Initialize Winsock (must be called before using port functions).
// Returns true on success, false on failure.
bool init_winsock();

// Check if a local TCP port is available for binding.
// Returns true if port is free, false if in use or error.
bool is_port_available(std::wstring_view host, uint16_t port);

// Find an available ephemeral port in range [49152, 65535].
// Returns 0 if none found.
uint16_t find_available_port(std::wstring_view host = L"127.0.0.1");

// ------------------------------------------------------------
// Helper: build SSH command line for tunnel
// ------------------------------------------------------------

// Build: ssh -L [bind_host:]local_port:remote_host:remote_port [options] user@ssh_host
std::vector<std::wstring> build_tunnel_ssh_args(const TunnelConfig& cfg);

} // namespace ssh_tunnel