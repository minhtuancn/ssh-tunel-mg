#include "ssh_tunnel.h"
#include <cassert>
#include <string>
#include <winsock2.h>

using namespace ssh_tunnel;

void test_tunnel_config_defaults() {
    TunnelConfig cfg;
    assert(cfg.local_host.empty());
    assert(cfg.local_port == 0);
    assert(cfg.remote_host.empty());
    assert(cfg.remote_port == 0);
    assert(cfg.ssh_host.empty());
    assert(cfg.ssh_port == 22);
    assert(cfg.ssh_user.empty());
    assert(cfg.identity_file.empty());
    assert(cfg.use_agent == true);
    assert(cfg.enable_compression == false);
    assert(cfg.keepalive_interval_sec == 30);
    assert(cfg.connect_timeout_sec == 15);
}

void test_tunnel_info_defaults() {
    TunnelInfo info;
    assert(info.status == TunnelStatus::STOPPED);
    assert(info.last_error == TunnelError::NONE);
    assert(info.error_message.empty());
    assert(info.bound_local_port == 0);
    assert(info.ssh_pid == 0);
    assert(info.bytes_sent == 0);
    assert(info.bytes_received == 0);
}

void test_build_tunnel_ssh_args_basic() {
    TunnelConfig cfg;
    cfg.ssh_host = L"example.com";
    cfg.ssh_user = L"testuser";
    cfg.ssh_port = 22;
    cfg.local_host = L"127.0.0.1";
    cfg.local_port = 8080;
    cfg.remote_host = L"127.0.0.1";
    cfg.remote_port = 80;
    
    auto args = build_tunnel_ssh_args(cfg);
    
    assert(!args.empty());
    assert(args[0] == L"ssh");
    
    // Check for -L argument
    bool found_L = false;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == L"-L" && i + 1 < args.size()) {
            found_L = true;
            assert(args[i + 1] == L"127.0.0.1:8080:127.0.0.1:80");
            break;
        }
    }
    assert(found_L);
    
    // Check for user@host
    bool found_host = false;
    for (const auto& arg : args) {
        if (arg == L"testuser@example.com") {
            found_host = true;
            break;
        }
    }
    assert(found_host);
}

void test_build_tunnel_ssh_args_with_key() {
    TunnelConfig cfg;
    cfg.ssh_host = L"example.com";
    cfg.ssh_user = L"testuser";
    cfg.identity_file = L"/home/user/.ssh/id_rsa";
    cfg.local_port = 8080;
    cfg.remote_host = L"127.0.0.1";
    cfg.remote_port = 80;
    
    auto args = build_tunnel_ssh_args(cfg);
    
    bool found_key = false;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == L"-i" && i + 1 < args.size()) {
            found_key = true;
            assert(args[i + 1] == L"/home/user/.ssh/id_rsa");
            break;
        }
    }
    assert(found_key);
}

void test_build_tunnel_ssh_args_compression() {
    TunnelConfig cfg;
    cfg.ssh_host = L"example.com";
    cfg.ssh_user = L"testuser";
    cfg.enable_compression = true;
    cfg.local_port = 8080;
    cfg.remote_host = L"127.0.0.1";
    cfg.remote_port = 80;
    
    auto args = build_tunnel_ssh_args(cfg);
    
    bool found_C = false;
    for (const auto& arg : args) {
        if (arg == L"-C") {
            found_C = true;
            break;
        }
    }
    assert(found_C);
}

void test_build_tunnel_ssh_args_keepalive() {
    TunnelConfig cfg;
    cfg.ssh_host = L"example.com";
    cfg.ssh_user = L"testuser";
    cfg.keepalive_interval_sec = 60;
    cfg.local_port = 8080;
    cfg.remote_host = L"127.0.0.1";
    cfg.remote_port = 80;
    
    auto args = build_tunnel_ssh_args(cfg);
    
    bool found_keepalive = false;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == L"-o" && i + 1 < args.size() && 
            args[i + 1].find(L"ServerAliveInterval=60") != std::wstring::npos) {
            found_keepalive = true;
            break;
        }
    }
    assert(found_keepalive);
}

void test_build_tunnel_ssh_args_connect_timeout() {
    TunnelConfig cfg;
    cfg.ssh_host = L"example.com";
    cfg.ssh_user = L"testuser";
    cfg.connect_timeout_sec = 30;
    cfg.local_port = 8080;
    cfg.remote_host = L"127.0.0.1";
    cfg.remote_port = 80;
    
    auto args = build_tunnel_ssh_args(cfg);
    
    bool found_timeout = false;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == L"-o" && i + 1 < args.size() && 
            args[i + 1].find(L"ConnectTimeout=30") != std::wstring::npos) {
            found_timeout = true;
            break;
        }
    }
    assert(found_timeout);
}

void test_is_port_available() {
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(listener != INVALID_SOCKET);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // let the OS pick a free port
    assert(bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    assert(listen(listener, 1) == 0);

    int addr_len = sizeof(addr);
    assert(getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &addr_len) == 0);
    const uint16_t bound_port = ntohs(addr.sin_port);
    assert(bound_port != 0);

    // A port with an active listener must be reported as unavailable.
    assert(!is_port_available(L"127.0.0.1", bound_port));

    closesocket(listener);

    // Once released, the same port should be bindable again.
    assert(is_port_available(L"127.0.0.1", bound_port));
}

void test_find_available_port() {
    uint16_t port = find_available_port(L"127.0.0.1");
    assert(port >= 49152 && port <= 65535);
    assert(is_port_available(L"127.0.0.1", port));
}

int main() {
    // Initialize Winsock before running port tests
    assert(ssh_tunnel::init_winsock());
    
    test_tunnel_config_defaults();
    test_tunnel_info_defaults();
    test_build_tunnel_ssh_args_basic();
    test_build_tunnel_ssh_args_with_key();
    test_build_tunnel_ssh_args_compression();
    test_build_tunnel_ssh_args_keepalive();
    test_build_tunnel_ssh_args_connect_timeout();
    test_is_port_available();
    test_find_available_port();
    return 0;
}