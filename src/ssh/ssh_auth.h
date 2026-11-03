#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ssh_tunnel {

// ------------------------------------------------------------
// Result codes for authentication / preflight outcomes
// ------------------------------------------------------------

enum class AuthStatus : uint32_t {
    OK = 0,               // Authentication succeeded (BatchMode yes preflight)
    FAIL,                 // Authentication failed (bad password / key rejected)
    TIMEOUT,              // Connection timed out
    DEPENDENCY_MISSING,   // Bundled OpenSSH binary not found / corrupt
    INTEGRITY_FAILURE,    // Binary SHA256 mismatch / tampered
    HOST_KEY_UNKNOWN,     // Host key not in known_hosts; onboarding needed
    HOST_KEY_CHANGED,     // Host key changed in known_hosts
    AGENT_UNAVAILABLE,    // ssh-agent / Pageant not running
    KEY_NOT_LOADED,       // Private key not loaded into agent
    KEY_PASSPHRASE_REQUIRED, // Key requires passphrase and no agent
    INVALID_CONFIG,       // Malformed SSH config
    CANCELLED,            // User cancelled onboarding / preflight
};

// ------------------------------------------------------------
// SSH config entry parsed from %USERPROFILE%\.ssh\config
// ------------------------------------------------------------

struct SshConfig {
    std::wstring host_pattern = L"*";  // e.g. "dev*", "*", or literal hostname
    std::wstring hostname;        // resolved HostName
    uint16_t port = 22;           // resolved Port (default 22)
    std::wstring user;            // resolved User
    std::wstring identity_file;   // resolved IdentityFile path
    bool proxy_jump = false;      // resolved ProxyJump / -J flag
};

// Parse the user's SSH config at %USERPROFILE%\.ssh\config.
// Only reads: Host, HostName, Port, User, IdentityFile, ProxyJump.
// Ignores: ForwardAgent, ForwardX11, etc. (security: no shell metachar resolution).
AuthStatus parse_ssh_config(std::wstring_view config_path, SshConfig& out);

// ------------------------------------------------------------
// Authentication context – owned by the caller (e.g. UI layer)
// ------------------------------------------------------------

class AuthContext {
public:
    AuthContext();
    ~AuthContext();

    // Load a private key file (public key portion may be inspected;
    // the raw private key is never stored in DB or logged).
    // Returns KEY_NOT_LOADED if the file cannot be read,
    // KEY_PASSPHRASE_REQUIRED if the key is encrypted and no agent
    // is available, or OK on success.
    AuthStatus load_key(std::wstring_view key_path);

    // Indicate that ssh-agent (or Windows Pageant) is available.
    // If the key was already added, subsequent preflights will succeed
    // without prompting for passphrase.
    void set_agent_available(bool available);

    // Was a key successfully loaded into the agent?
    bool key_loaded() const noexcept;

    // Was the key passphrase-protected and required agent interaction?
    bool passphrase_required() const noexcept;

    // The identity file path that was loaded (empty if none).
    std::wstring identity_file_path() const noexcept;

private:
    bool agent_available_ = false;
    bool key_loaded_ = false;
    bool passphrase_required_ = false;
    std::wstring identity_file_path_;
};

// ------------------------------------------------------------
// Run a BatchMode=yes preflight against a SSH host.
// The binary used is the "bundled" OpenSSH (path resolved from
// resources/openssh/ or the system ssh.exe if available; the caller
// must respect DEPENDENCY_MISSING if no binary is on disk).
// ------------------------------------------------------------

// Launch: ssh -B BatchMode=yes -T user@host  (or with -p port)
// Returns AuthStatus.  The caller should display HOST_KEY_UNKNOWN
// as a console onboarding prompt (fingerprint verification via
// trusted channel).  After the user accepts, re-run this preflight
// to get AUTH_STATUS_OK.
AuthStatus run_batch_mode_preflight(
    const SshConfig& config,
    std::atomic<bool>* cancel_requested = nullptr,
    // If non-null, the function may poll this flag and abort early.
    // The API does NOT create a worker thread – the caller must not
    // block the UI for the duration of the SSH connection.
    const std::wstring& bundled_ssh_path = L"");

// Convenience: extract just the host alias / pattern from a config
// line or command-line argument (used for display/labeling).
std::wstring extract_host_alias(std::wstring_view arg);

// ------------------------------------------------------------
// Exit-status mapping for common SSH exit codes
// ------------------------------------------------------------

/// Map a known SSH exit code to an AuthStatus.
/// Exit codes 0..3 are from OpenSSH; others are mapped to FAIL.
AuthStatus ssh_exit_to_auth_status(int exit_code);

} // namespace ssh_tunnel