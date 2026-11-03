#include "ssh_tunnel.h"
#include "ssh_process.h"
#include "ssh_arguments.h"
#include "ssh_auth.h"
#include "ssh_known_hosts.h"
// winsock2.h must be included before windows.h.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <chrono>
#include <atomic>

#pragma comment(lib, "ws2_32.lib")

using namespace ssh_tunnel;

// ------------------------------------------------------------
// Port availability checking
// ------------------------------------------------------------

// Winsock initialization - call ssh_tunnel::init_winsock() explicitly from main() before using port functions
// No global state - caller manages Winsock lifecycle

bool ssh_tunnel::init_winsock()
{
    WSADATA wsa;
    int result = WSAStartup(MAKEWORD(2, 2), &wsa);
    return result == 0;
}

bool ssh_tunnel::is_port_available(std::wstring_view host, uint16_t port)
{

    struct addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICHOST | AI_PASSIVE;

    std::string host_str(host.begin(), host.end());
    std::string port_str = std::to_string(port);

    int err = getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &res);
    if (err != 0)
        return false;

    bool available = false;
    for (struct addrinfo* rp = res; rp != nullptr; rp = rp->ai_next) {
        SOCKET s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (s == INVALID_SOCKET)
            continue;

        // SO_REUSEADDR on Windows lets us bind a port that another process is
        // already listening on, which would report busy ports as available.
        // Use exclusive mode so a conflicting listener makes bind() fail.
        BOOL exclusive = TRUE;
        setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));

        if (bind(s, rp->ai_addr, static_cast<int>(rp->ai_addrlen)) == 0) {
            available = true;
        }
        closesocket(s);
        if (available) break;
    }
    freeaddrinfo(res);
    return available;
}

uint16_t ssh_tunnel::find_available_port(std::wstring_view host)
{
    for (uint16_t port = 49152; port <= 65535; ++port) {
        if (ssh_tunnel::is_port_available(host, port))
            return port;
    }
    return 0;
}

// ------------------------------------------------------------
// Build SSH command line for tunnel
// ------------------------------------------------------------

std::vector<std::wstring> ssh_tunnel::build_tunnel_ssh_args(const TunnelConfig& cfg)
{
    ssh_tunnel::ArgBuilderFlags flags = ssh_tunnel::ArgBuilderFlags::BATCH_MODE | ssh_tunnel::ArgBuilderFlags::STRICT_KEYS;

    ssh_tunnel::ArgBuilder builder(flags);
    builder.set_host(cfg.ssh_host);
    builder.set_user(cfg.ssh_user);
    builder.set_port(cfg.ssh_port);

    if (!cfg.identity_file.empty()) {
        builder.set_identity_file(cfg.identity_file);
    }

    // Build -L argument: [bind_host:]local_port:remote_host:remote_port
    std::wstring l_arg;
    if (!cfg.local_host.empty() && cfg.local_host != L"0.0.0.0") {
        l_arg = cfg.local_host + L":";
    }
    l_arg += std::to_wstring(cfg.local_port) + L":";
    l_arg += cfg.remote_host + L":";
    l_arg += std::to_wstring(cfg.remote_port);

    auto args = builder.build();

    // Find the destination host: the last non-option argument that is not the
    // value of a preceding option. Insert -L immediately before it.
    size_t host_idx = args.size();
    for (size_t i = args.size(); i-- > 0;) {
        const std::wstring& s = args[i];
        if (s == L"ssh" || (!s.empty() && s.front() == L'-')) continue;
        if (i > 0 && (args[i - 1] == L"-o" || args[i - 1] == L"-i" || args[i - 1] == L"-p")) continue;
        host_idx = i;
        break;
    }
    if (host_idx < args.size()) {
        args.insert(args.begin() + static_cast<std::ptrdiff_t>(host_idx),
                    {L"-L", l_arg});
    }

    // Build extra options once, then insert them as a block after "ssh" so
    // vector reallocation cannot invalidate an iterator between inserts.
    std::vector<std::wstring> extra;
    if (cfg.enable_compression) {
        extra.push_back(L"-C");
    }
    if (cfg.keepalive_interval_sec > 0) {
        extra.push_back(L"-o");
        extra.push_back(L"ServerAliveInterval=" + std::to_wstring(cfg.keepalive_interval_sec));
        extra.push_back(L"-o");
        extra.push_back(L"ServerAliveCountMax=3");
    }
    if (cfg.connect_timeout_sec > 0) {
        extra.push_back(L"-o");
        extra.push_back(L"ConnectTimeout=" + std::to_wstring(cfg.connect_timeout_sec));
    }

    auto ssh_it = std::find(args.begin(), args.end(), L"ssh");
    if (!extra.empty() && ssh_it != args.end()) {
        const size_t insert_at = static_cast<size_t>(std::distance(args.begin(), ssh_it)) + 1;
        args.insert(args.begin() + static_cast<std::ptrdiff_t>(insert_at),
                    extra.begin(), extra.end());
    }

    return args;
}

// ------------------------------------------------------------
// Tunnel implementation (PIMPL)
// ------------------------------------------------------------

struct Tunnel::Impl {
    TunnelConfig config;
    mutable std::mutex mtx;
    TunnelInfo info;
    std::thread worker_thread;
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> worker_running{false};
    std::condition_variable cv;
    ssh_tunnel::ProcessConfig proc_config;
    std::wstring ssh_executable;

    Impl(const TunnelConfig& cfg) : config(cfg), ssh_executable(L"resources\\openssh\\ssh.exe") {
        info.status = TunnelStatus::STOPPED;
        info.last_error = TunnelError::NONE;
    }

    ~Impl() {
        stop_requested.store(true);
        // Always join, even if the worker already finished: a joinable
        // std::thread must not be destroyed.
        if (worker_thread.joinable()) {
            worker_thread.join();
        }
    }

    void worker_main(std::atomic<bool>* external_cancel) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            info.status = TunnelStatus::STARTING;
            info.started_at = std::chrono::steady_clock::now();
        }

        // Run preflight first
        ssh_tunnel::SshConfig ssh_cfg;
        ssh_cfg.host_pattern = config.ssh_host;
        ssh_cfg.hostname = config.ssh_host;
        ssh_cfg.port = config.ssh_port;
        ssh_cfg.user = config.ssh_user;
        ssh_cfg.identity_file = config.identity_file;
        ssh_cfg.proxy_jump = false;

        ssh_tunnel::AuthContext auth_ctx;
        if (!config.identity_file.empty()) {
            auth_ctx.load_key(config.identity_file);
        }
        auth_ctx.set_agent_available(config.use_agent);

        ssh_tunnel::PreflightResult preflight = ssh_tunnel::run_full_preflight(ssh_cfg, auth_ctx, external_cancel);
        
        if (preflight.status != ssh_tunnel::AuthStatus::OK) {
            std::lock_guard<std::mutex> lock(mtx);
            info.status = TunnelStatus::ERROR_STATE;
            info.last_error = [preflight]() -> TunnelError {
                switch (preflight.status) {
                    case ssh_tunnel::AuthStatus::HOST_KEY_UNKNOWN: return TunnelError::HOST_KEY_UNKNOWN;
                    case ssh_tunnel::AuthStatus::HOST_KEY_CHANGED: return TunnelError::HOST_KEY_CHANGED;
                    case ssh_tunnel::AuthStatus::FAIL: return TunnelError::SSH_AUTH_FAILED;
                    case ssh_tunnel::AuthStatus::TIMEOUT: return TunnelError::TIMEOUT;
                    case ssh_tunnel::AuthStatus::CANCELLED: return TunnelError::CANCELLED;
                    case ssh_tunnel::AuthStatus::KEY_PASSPHRASE_REQUIRED: return TunnelError::SSH_AUTH_FAILED;
                    case ssh_tunnel::AuthStatus::AGENT_UNAVAILABLE: return TunnelError::SSH_AUTH_FAILED;
                    case ssh_tunnel::AuthStatus::KEY_NOT_LOADED: return TunnelError::SSH_AUTH_FAILED;
                    default: return TunnelError::SSH_PROCESS_FAILED;
                }
            }();
            info.error_message = L"Preflight failed";
            worker_running.store(false);
            cv.notify_all();
            return;
        }

        // Build process config
        proc_config.executable = ssh_executable;
        proc_config.arguments = ssh_tunnel::build_tunnel_ssh_args(config);
        proc_config.timeoutMs = 0;
        proc_config.stdoutLimitBytes = 1024 * 1024;
        proc_config.stderrLimitBytes = 256 * 1024;

        {
            std::lock_guard<std::mutex> lock(mtx);
            info.status = TunnelStatus::RUNNING;
            info.bound_local_port = config.local_port;
        }

        // Run SSH process. stop_requested is the tunnel's own flag; the
        // caller-supplied cancellation token is checked alongside it.
        ssh_tunnel::ProcessResult result =
            ssh_tunnel::RunSshProcess(proc_config, stop_requested, external_cancel);

        std::lock_guard<std::mutex> lock(mtx);
        if (stop_requested.load() || (external_cancel && external_cancel->load())) {
            info.status = TunnelStatus::STOPPED;
            info.last_error = TunnelError::CANCELLED;
        } else if (result.timedOut) {
            info.status = TunnelStatus::ERROR_STATE;
            info.last_error = TunnelError::TIMEOUT;
        } else if (result.cancelled) {
            info.status = TunnelStatus::STOPPED;
            info.last_error = TunnelError::CANCELLED;
        } else if (result.exitCode != 0) {
            info.status = TunnelStatus::ERROR_STATE;
            info.last_error = TunnelError::SSH_PROCESS_FAILED;
            info.error_message = L"SSH exited with code " + std::to_wstring(result.exitCode);
        } else {
            info.status = TunnelStatus::STOPPED;
        }
        info.ssh_pid = 0;

        worker_running.store(false);
        cv.notify_all();
    }
};

Tunnel::Tunnel(const TunnelConfig& cfg) : pimpl_(std::make_unique<Impl>(cfg)) {}
Tunnel::~Tunnel() = default;

Tunnel::Tunnel(Tunnel&& other) noexcept : pimpl_(std::move(other.pimpl_)) {}
Tunnel& Tunnel::operator=(Tunnel&& other) noexcept {
    pimpl_ = std::move(other.pimpl_);
    return *this;
}

void Tunnel::start(std::atomic<bool>* cancel_requested) {
    if (pimpl_->worker_thread.joinable()) {
        if (pimpl_->worker_running.load()) return;
        // Previous worker finished; reclaim it before starting a new one.
        pimpl_->worker_thread.join();
    }
    pimpl_->stop_requested.store(false);
    pimpl_->worker_running.store(true);
    pimpl_->worker_thread = std::thread(&Impl::worker_main, pimpl_.get(), cancel_requested);
}

void Tunnel::stop() {
    pimpl_->stop_requested.store(true);
    if (pimpl_->worker_thread.joinable()) {
        pimpl_->worker_thread.join();
    }
}

void Tunnel::kill() {
    // The worker terminates the SSH child when stop_requested is observed.
    pimpl_->stop_requested.store(true);
    if (pimpl_->worker_thread.joinable()) {
        pimpl_->worker_thread.join();
    }
}

TunnelInfo Tunnel::get_info() const noexcept {
    std::lock_guard<std::mutex> lock(pimpl_->mtx);
    return pimpl_->info;
}

TunnelStatus Tunnel::wait_for_status(TunnelStatus target, uint32_t timeout_ms) {
    std::unique_lock<std::mutex> lock(pimpl_->mtx);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    
    while (pimpl_->info.status != target) {
        if (pimpl_->cv.wait_until(lock, deadline) == std::cv_status::timeout) {
            break;
        }
    }
    return pimpl_->info.status;
}

bool Tunnel::is_healthy() const noexcept {
    std::lock_guard<std::mutex> lock(pimpl_->mtx);
    return pimpl_->info.status == TunnelStatus::RUNNING;
}

std::vector<std::wstring> Tunnel::get_ssh_command_line() const {
    return ssh_tunnel::build_tunnel_ssh_args(pimpl_->config);
}

// ------------------------------------------------------------
// TunnelManager implementation
// ------------------------------------------------------------

struct TunnelManager::Impl {
    std::mutex mtx;
    std::vector<std::unique_ptr<Tunnel>> tunnels;
};

TunnelManager::TunnelManager() : pimpl_(std::make_unique<Impl>()) {}
TunnelManager::~TunnelManager() = default;

size_t TunnelManager::add_tunnel(const TunnelConfig& cfg) {
    std::lock_guard<std::mutex> lock(pimpl_->mtx);
    pimpl_->tunnels.push_back(std::make_unique<Tunnel>(cfg));
    return pimpl_->tunnels.size() - 1;
}

bool TunnelManager::start_tunnel(size_t id, std::atomic<bool>* cancel_requested) {
    std::lock_guard<std::mutex> lock(pimpl_->mtx);
    if (id >= pimpl_->tunnels.size()) return false;
    auto& tunnel = pimpl_->tunnels[id];
    if (tunnel->get_info().status == TunnelStatus::RUNNING) return true;
    tunnel->start(cancel_requested);
    return true;
}

bool TunnelManager::stop_tunnel(size_t id) {
    std::lock_guard<std::mutex> lock(pimpl_->mtx);
    if (id >= pimpl_->tunnels.size()) return false;
    pimpl_->tunnels[id]->stop();
    return true;
}

bool TunnelManager::remove_tunnel(size_t id) {
    std::lock_guard<std::mutex> lock(pimpl_->mtx);
    if (id >= pimpl_->tunnels.size()) return false;
    auto& tunnel = pimpl_->tunnels[id];
    if (tunnel->get_info().status == TunnelStatus::RUNNING) {
        return false;
    }
    pimpl_->tunnels.erase(pimpl_->tunnels.begin() + id);
    return true;
}

TunnelInfo TunnelManager::get_tunnel_info(size_t id) const {
    std::lock_guard<std::mutex> lock(pimpl_->mtx);
    if (id >= pimpl_->tunnels.size()) {
        return TunnelInfo{};
    }
    return pimpl_->tunnels[id]->get_info();
}

std::vector<TunnelInfo> TunnelManager::get_all_tunnel_infos() const {
    std::lock_guard<std::mutex> lock(pimpl_->mtx);
    std::vector<TunnelInfo> infos;
    infos.reserve(pimpl_->tunnels.size());
    for (const auto& t : pimpl_->tunnels) {
        infos.push_back(t->get_info());
    }
    return infos;
}

size_t TunnelManager::count() const noexcept {
    std::lock_guard<std::mutex> lock(pimpl_->mtx);
    return pimpl_->tunnels.size();
}

size_t TunnelManager::running_count() const noexcept {
    std::lock_guard<std::mutex> lock(pimpl_->mtx);
    size_t n = 0;
    for (const auto& t : pimpl_->tunnels) {
        if (t->get_info().status == TunnelStatus::RUNNING) ++n;
    }
    return n;
}