#include "ssh_auth.h"
#include <cassert>
#include <string>
#include <vector>

using namespace ssh_tunnel;

void test_auth_status_values() {
    // Verify enum values are distinct
    assert(static_cast<int>(AuthStatus::OK) == 0);
    assert(static_cast<int>(AuthStatus::FAIL) == 1);
    assert(static_cast<int>(AuthStatus::TIMEOUT) == 2);
    assert(static_cast<int>(AuthStatus::DEPENDENCY_MISSING) == 3);
    assert(static_cast<int>(AuthStatus::INTEGRITY_FAILURE) == 4);
    assert(static_cast<int>(AuthStatus::HOST_KEY_UNKNOWN) == 5);
    assert(static_cast<int>(AuthStatus::HOST_KEY_CHANGED) == 6);
    assert(static_cast<int>(AuthStatus::AGENT_UNAVAILABLE) == 7);
    assert(static_cast<int>(AuthStatus::KEY_NOT_LOADED) == 8);
    assert(static_cast<int>(AuthStatus::KEY_PASSPHRASE_REQUIRED) == 9);
    assert(static_cast<int>(AuthStatus::INVALID_CONFIG) == 10);
    assert(static_cast<int>(AuthStatus::CANCELLED) == 11);
}

void test_ssh_exit_to_auth_status() {
    assert(ssh_exit_to_auth_status(0) == AuthStatus::OK);
    assert(ssh_exit_to_auth_status(1) == AuthStatus::FAIL);
    assert(ssh_exit_to_auth_status(2) == AuthStatus::FAIL);
    assert(ssh_exit_to_auth_status(3) == AuthStatus::FAIL);
    assert(ssh_exit_to_auth_status(126) == AuthStatus::FAIL);
    assert(ssh_exit_to_auth_status(127) == AuthStatus::FAIL);
    assert(ssh_exit_to_auth_status(255) == AuthStatus::FAIL);
    assert(ssh_exit_to_auth_status(-1) == AuthStatus::FAIL);
}

void test_extract_host_alias() {
    assert(extract_host_alias(L"user@host") == L"host");
    assert(extract_host_alias(L"host") == L"host");
    assert(extract_host_alias(L"host:2222") == L"host");
    assert(extract_host_alias(L"user@host:2222") == L"host");
    assert(extract_host_alias(L"192.168.1.1") == L"192.168.1.1");
    assert(extract_host_alias(L"[::1]:22") == L"[::1]");
}

void test_ssh_config_defaults() {
    SshConfig cfg;
    assert(cfg.host_pattern == L"*");
    assert(cfg.hostname.empty());
    assert(cfg.port == 22);
    assert(cfg.user.empty());
    assert(cfg.identity_file.empty());
    assert(cfg.proxy_jump == false);
}

void test_auth_context() {
    AuthContext ctx;
    assert(ctx.key_loaded() == false);
    assert(ctx.passphrase_required() == false);
    assert(ctx.identity_file_path().empty());
    assert(ctx.key_loaded() == false);
    
    ctx.set_agent_available(true);
    // Can't test load_key without actual key files
}

int main() {
    test_auth_status_values();
    test_ssh_exit_to_auth_status();
    test_extract_host_alias();
    test_ssh_config_defaults();
    test_auth_context();
    return 0;
}