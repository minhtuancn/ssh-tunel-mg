#pragma once

#include "ssh_auth.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ssh_tunnel {

// ------------------------------------------------------------
// Host key types and storage (known_hosts)
// ------------------------------------------------------------

enum class HostKeyType : uint32_t {
    UNKNOWN = 0,
    RSA     = 1,
    ECDSA   = 2,
    ED25519 = 3,
};

struct HostKeyEntry {
    std::wstring hostname;        // canonical hostname or IP
    uint16_t port = 22;           // port (default 22)
    HostKeyType type = HostKeyType::UNKNOWN;
    std::string fingerprint_sha256;  // base64-encoded SHA256 (e.g. "SHA256:xxxx")
    std::string public_key_blob;     // raw public key (base64) for comparison
    bool verified = false;           // user explicitly verified via onboarding
};

// Parse a single line from known_hosts.
// Returns true on success, false on malformed line (ignored).
bool parse_known_hosts_line(std::string_view line, HostKeyEntry& out);

// Load known_hosts from %USERPROFILE%\.ssh\known_hosts.
// Returns entries; empty if file missing/unreadable.
std::vector<HostKeyEntry> load_known_hosts(std::wstring_view path = L"");

// Look up a host key entry for (hostname, port).
// Returns pointer to entry if found (may have verified=false).
// Returns nullptr if no entry exists for this host/port.
const HostKeyEntry* find_host_key(const std::vector<HostKeyEntry>& entries,
                                  std::wstring_view hostname, uint16_t port);

// Check if the remote host's presented key matches known_hosts.
// Called after SSH connection attempt when we have the remote fingerprint.
// Returns:
//   OK                - key matches a verified entry
//   HOST_KEY_UNKNOWN  - no entry in known_hosts (onboarding needed)
//   HOST_KEY_CHANGED  - entry exists but fingerprint differs (blocking)
AuthStatus verify_host_key(const std::vector<HostKeyEntry>& known,
                           std::wstring_view hostname, uint16_t port,
                           std::string_view remote_fingerprint_sha256,
                           HostKeyType remote_type);

// Add a new host key entry after user verifies fingerprint.
// Appends to known_hosts file (creates if missing).
// Returns OK on success, FAIL on write error.
AuthStatus add_host_key(std::wstring_view known_hosts_path,
                        const HostKeyEntry& entry);

// Update an existing entry's fingerprint (e.g. after rotation).
// Only allowed if the caller has explicit confirmation.
AuthStatus update_host_key(std::wstring_view known_hosts_path,
                           std::wstring_view hostname, uint16_t port,
                           std::string_view new_fingerprint_sha256,
                           HostKeyType new_type);

// ------------------------------------------------------------
// Onboarding console flow (text-mode UI for fingerprint verification)
// ------------------------------------------------------------

struct OnboardingResult {
    AuthStatus status;           // OK, CANCELLED, HOST_KEY_CHANGED, etc.
    std::string fingerprint;     // the fingerprint that was presented
    std::wstring hostname;
    uint16_t port;
};

// Run the interactive onboarding console for an unknown host key.
// Prompts user to verify SHA256 fingerprint via trusted channel.
// Returns OnboardingResult with status:
//   OK          - user typed "yes" and fingerprint recorded
//   CANCELLED   - user typed "no" or closed console
//   FAIL        - I/O error
OnboardingResult run_onboarding_console(std::wstring_view hostname,
                                        uint16_t port,
                                        HostKeyType key_type,
                                        std::string_view fingerprint_sha256,
                                        std::atomic<bool>* cancel_requested = nullptr);

// Run onboarding for a CHANGED host key (blocking error - requires explicit
// "yes I understand the risk" with fingerprint comparison display).
// Returns CANCELLED if user declines; OK if user accepts the new key.
OnboardingResult run_changed_key_console(std::wstring_view hostname,
                                         uint16_t port,
                                         HostKeyType key_type,
                                         std::string_view old_fingerprint,
                                         std::string_view new_fingerprint,
                                         std::atomic<bool>* cancel_requested = nullptr);

// ------------------------------------------------------------
// Preflight orchestration: runs BatchMode=yes, handles host key states
// ------------------------------------------------------------

struct PreflightResult {
    AuthStatus status;               // OK, HOST_KEY_UNKNOWN, HOST_KEY_CHANGED, FAIL, TIMEOUT, etc.
    std::string remote_fingerprint;  // SHA256 fingerprint from SSH (if available)
    HostKeyType remote_type;         // key type from SSH
    std::wstring hostname;
    uint16_t port;
    bool onboarding_completed = false;  // true if onboarding ran and user accepted
};

// Full preflight: SSH config → bundled ssh.exe → BatchMode=yes → host key check.
// If HOST_KEY_UNKNOWN, runs onboarding console.
// If HOST_KEY_CHANGED, runs changed-key console (blocking).
// Returns final status after possible onboarding.
PreflightResult run_full_preflight(const SshConfig& config,
                                   const AuthContext& auth_ctx,
                                   std::atomic<bool>* cancel_requested = nullptr);

} // namespace ssh_tunnel