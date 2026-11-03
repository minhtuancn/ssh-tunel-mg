#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ssh_tunnel {

// ------------------------------------------------------------
// Flags controlling how command-line arguments are built.
// ------------------------------------------------------------

enum class ArgBuilderFlags : uint32_t {
    NONE         = 0,
    BATCH_MODE   = 1 << 0,  // append -o BatchMode=yes
    STRICT_KEYS  = 1 << 1,  // do NOT add -o StrictHostKeyChecking=no
    AGENT_FORWARD = 1 << 2, // -o ForwardAgent=yes (NOT enabled by default)
};

// ------------------------------------------------------------
// Safe argument builder for SSH commands.
// Produces a wchar_t** argv suitable for CreateProcessW / exec.
// Guarantees no shell metacharacters are present; every token is
// individually quoted/escaped so the OS never passes a shell.
// ------------------------------------------------------------

class ArgBuilder {
public:
    // Build arguments for: ssh [flags] [user@]host [-p port] [cmd]
    // If |cmd| is non-empty, it is appended as a remote command
    // (ssh host "cmd") – in that case the whole command string is
    // passed as a single quoted token to avoid shell interpretation.
    ArgBuilder(ArgBuilderFlags flags = ArgBuilderFlags::NONE);

    // Set the SSH command host (required).
    void set_host(std::wstring_view host);

    // Set the SSH user (optional).
    void set_user(std::wstring_view user);

    // Set the port (default 22; call set_port(0) to leave default).
    void set_port(uint16_t port);

    // Set an explicit IdentityFile path (optional).
    void set_identity_file(std::wstring_view path);

    // Set a remote command to execute (e.g. "ls -l").
    // If set, the builder produces: ssh host "command"
    // (the command token is fully quoted so the child process receives
    // the literal string).
    void set_remote_command(std::wstring_view cmd);

    // Append an arbitrary -o option (e.g. "BatchMode=yes").
    // Validates that the option string contains no shell metacharacters.
    void add_option(std::wstring_view option);

    // Core validation: reject any string that contains shell metacharacters.
    // Public so callers and tests can validate input before building.
    static bool contains_shell_metacharacters(std::wstring_view s);

    // Finalise and return the argument array. Tokens are returned raw;
    // Windows command-line quoting is applied by RunSshProcess when the
    // child process is created. Do not pre-quote here or the child would
    // receive literal quote characters.
    std::vector<std::wstring> build();

    // Convenience: return a C-style array (argv style).  The caller must
    // free/delete as appropriate.  Use build() instead if possible.
    const std::wstring* data() const noexcept { return items_.data(); }
    size_t count() const noexcept { return items_.size(); }

private:
    ArgBuilderFlags flags_;

    std::wstring host_;
    std::wstring user_;
    uint16_t port_;      // 0 means "default (22)"
    std::wstring identity_file_;
    std::wstring remote_command_;

    std::vector<std::wstring> items_;  // accumulated argv items
};

// ------------------------------------------------------------
// Convenience: build a minimal SSH command for BatchMode=yes preflight.
// ------------------------------------------------------------

// Build: ssh -o BatchMode=yes -o StrictHostKeyChecking=yes user@host [-p port]
// Returns a vector<wstring> argv with nullptr terminator; caller must not
// free individual strings (they are owned by the vector).
std::vector<std::wstring> build_ssh_preflight_args(
    std::wstring_view host,
    std::wstring_view user = L"",
    uint16_t port = 22,
    ArgBuilderFlags flags = ArgBuilderFlags::NONE);

// Build: ssh -o BatchMode=yes -o StrictHostKeyChecking=yes -i keyfile user@host
std::vector<std::wstring> build_ssh_preflight_with_key_args(
    std::wstring_view host,
    std::wstring_view user = L"",
    uint16_t port = 22,
    std::wstring_view keyfile = L"",
    ArgBuilderFlags flags = ArgBuilderFlags::NONE);

// Bitwise operators for ArgBuilderFlags
inline ArgBuilderFlags operator|(ArgBuilderFlags a, ArgBuilderFlags b) {
    return static_cast<ArgBuilderFlags>(static_cast<int>(a) | static_cast<int>(b));
}
inline ArgBuilderFlags operator&(ArgBuilderFlags a, ArgBuilderFlags b) {
    return static_cast<ArgBuilderFlags>(static_cast<int>(a) & static_cast<int>(b));
}
inline ArgBuilderFlags operator^(ArgBuilderFlags a, ArgBuilderFlags b) {
    return static_cast<ArgBuilderFlags>(static_cast<int>(a) ^ static_cast<int>(b));
}
inline ArgBuilderFlags operator~(ArgBuilderFlags a) {
    return static_cast<ArgBuilderFlags>(~static_cast<int>(a));
}
inline ArgBuilderFlags& operator|=(ArgBuilderFlags& a, ArgBuilderFlags b) {
    a = a | b; return a;
}
inline ArgBuilderFlags& operator&=(ArgBuilderFlags& a, ArgBuilderFlags b) {
    a = a & b; return a;
}
inline ArgBuilderFlags& operator^=(ArgBuilderFlags& a, ArgBuilderFlags b) {
    a = a ^ b; return a;
}

} // namespace ssh_tunnel