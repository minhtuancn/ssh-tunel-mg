#include "ssh_known_hosts.h"
#include "ssh_process.h"
#include "ssh_arguments.h"
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

// ------------------------------------------------------------
// known_hosts parsing
// ------------------------------------------------------------

bool ssh_tunnel::parse_known_hosts_line(std::string_view line, ssh_tunnel::HostKeyEntry& out)
{
    if (line.empty() || line[0] == '#')
        return false;

    // Skip leading whitespace.
    size_t pos = 0;
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])))
        ++pos;
    if (pos >= line.size())
        return false;

    // Extract hostname field (may contain commas for multiple names).
    size_t space1 = line.find(' ', pos);
    if (space1 == std::string::npos)
        return false;
    std::string hostnames_field(line.substr(pos, space1 - pos));
    pos = space1 + 1;

    // Skip whitespace.
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])))
        ++pos;
    if (pos >= line.size())
        return false;

    // Extract keytype.
    size_t space2 = line.find(' ', pos);
    if (space2 == std::string::npos)
        return false;
    std::string keytype(line.substr(pos, space2 - pos));
    pos = space2 + 1;

    // Skip whitespace.
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])))
        ++pos;
    if (pos >= line.size())
        return false;

    // Extract base64 blob (rest of line until whitespace or end).
    size_t space3 = line.find(' ', pos);
    std::string blob;
    if (space3 == std::string::npos) {
        blob = std::string(line.substr(pos));
        pos = line.size();
    } else {
        blob = std::string(line.substr(pos, space3 - pos));
        pos = space3;
    }

    // Parse hostnames: comma-separated, each may be [host]:port or host
    std::vector<std::string> hosts;
    size_t start = 0;
    while (start < hostnames_field.size()) {
        size_t comma = hostnames_field.find(',', start);
        std::string h = hostnames_field.substr(start,
            (comma == std::string::npos ? hostnames_field.size() : comma) - start);
        hosts.push_back(h);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }

    // We'll use the first non-hashed host (hashed hosts start with '|').
    std::string chosen_host;
    uint16_t chosen_port = 22;
    for (const auto& h : hosts) {
        if (h.empty() || h[0] == '|')
            continue; // skip hashed
        // Check for [host]:port format.
        if (h[0] == '[') {
            size_t close = h.find(']');
            if (close != std::string::npos) {
                chosen_host = h.substr(1, close - 1);
                if (close + 1 < h.size() && h[close + 1] == ':') {
                    std::string port_str = h.substr(close + 2);
                    try { chosen_port = static_cast<uint16_t>(std::stoi(port_str)); } catch (...) {}
                }
                break;
            }
        } else {
            size_t colon = h.rfind(':');
            if (colon != std::string::npos && colon > 0) {
                std::string port_str = h.substr(colon + 1);
                try {
                    int p = std::stoi(port_str);
                    if (p >= 1 && p <= 65535) {
                        chosen_host = h.substr(0, colon);
                        chosen_port = static_cast<uint16_t>(p);
                        break;
                    }
                } catch (...) {}
            } else {
                chosen_host = h;
                chosen_port = 22;
                break;
            }
        }
    }
    if (chosen_host.empty())
        return false;

    // Map keytype to HostKeyType.
    ssh_tunnel::HostKeyType type = ssh_tunnel::HostKeyType::UNKNOWN;
    if (keytype == "ssh-rsa" || keytype == "rsa-sha2-256" || keytype == "rsa-sha2-512")
        type = ssh_tunnel::HostKeyType::RSA;
    else if (keytype.find("ecdsa") != std::string::npos)
        type = ssh_tunnel::HostKeyType::ECDSA;
    else if (keytype == "ssh-ed25519")
        type = ssh_tunnel::HostKeyType::ED25519;

    out.hostname = std::wstring(chosen_host.begin(), chosen_host.end());
    out.port = chosen_port;
    out.type = type;
    out.public_key_blob = blob;
    out.fingerprint_sha256 = "";
    out.verified = false;

    return true;
}

std::vector<ssh_tunnel::HostKeyEntry> ssh_tunnel::load_known_hosts(std::wstring_view path)
{
    std::vector<ssh_tunnel::HostKeyEntry> entries;

    std::wstring full_path;
    if (path.empty()) {
        wchar_t* up = nullptr;
        size_t sz = 0;
        errno_t err = _wdupenv_s(&up, &sz, L"USERPROFILE");
        if (err == 0 && up != nullptr) {
            full_path = up;
            full_path += L"\\.ssh\\known_hosts";
            LocalFree(up);
        }
    } else {
        full_path = std::wstring(path);
    }

    if (full_path.empty())
        return entries;

    HANDLE h = CreateFileW(full_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return entries;

    DWORD file_size = GetFileSize(h, nullptr);
    std::vector<char> buf(file_size + 1);
    DWORD nread = 0;
    ReadFile(h, buf.data(), file_size, &nread, nullptr);
    buf[nread] = '\0';
    CloseHandle(h);

    std::string content(buf.data(), nread);
    size_t line_start = 0;
    while (line_start < content.size()) {
        size_t line_end = content.find('\n', line_start);
        if (line_end == std::string::npos)
            line_end = content.size();
        std::string_view line(content.data() + line_start, line_end - line_start);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);

        ssh_tunnel::HostKeyEntry entry;
        if (ssh_tunnel::parse_known_hosts_line(line, entry)) {
            entries.push_back(entry);
        }
        line_start = line_end + 1;
    }

    return entries;
}

const ssh_tunnel::HostKeyEntry* ssh_tunnel::find_host_key(const std::vector<ssh_tunnel::HostKeyEntry>& entries,
                                  std::wstring_view hostname, uint16_t port)
{
    std::string h(hostname.begin(), hostname.end());
    for (const auto& e : entries) {
        if (e.hostname == std::wstring(h.begin(), h.end()) && e.port == port) {
            return &e;
        }
    }
    return nullptr;
}

// ------------------------------------------------------------
// SSH fingerprint extraction from stderr
// ----------------------------------------------------------

static bool extract_fingerprint_from_ssh_stderr(const std::string& stderr_data,
                                                std::string& out_fp,
                                                ssh_tunnel::HostKeyType& out_type)
{
    size_t pos = stderr_data.find("SHA256:");
    if (pos == std::string::npos)
        return false;

    size_t end = pos + 7;
    while (end < stderr_data.size() && !std::isspace(static_cast<unsigned char>(stderr_data[end])))
        ++end;
    out_fp = stderr_data.substr(pos, end - pos);

    if (stderr_data.find("ecdsa") != std::string::npos)
        out_type = ssh_tunnel::HostKeyType::ECDSA;
    else if (stderr_data.find("ed25519") != std::string::npos)
        out_type = ssh_tunnel::HostKeyType::ED25519;
    else
        out_type = ssh_tunnel::HostKeyType::RSA;

    return true;
}

// ------------------------------------------------------------
// verify_host_key
// ------------------------------------------------------------

ssh_tunnel::AuthStatus ssh_tunnel::verify_host_key(const std::vector<ssh_tunnel::HostKeyEntry>& known,
                           std::wstring_view hostname, uint16_t port,
                           std::string_view remote_fingerprint_sha256,
                           ssh_tunnel::HostKeyType remote_type)
{
    const ssh_tunnel::HostKeyEntry* entry = ssh_tunnel::find_host_key(known, hostname, port);
    if (!entry) {
        return ssh_tunnel::AuthStatus::HOST_KEY_UNKNOWN;
    }

    if (!remote_fingerprint_sha256.empty() && !entry->fingerprint_sha256.empty()) {
        if (remote_fingerprint_sha256 != entry->fingerprint_sha256) {
            return ssh_tunnel::AuthStatus::HOST_KEY_CHANGED;
        }
    }

    if (!entry->verified) {
        return ssh_tunnel::AuthStatus::HOST_KEY_UNKNOWN;
    }

    return ssh_tunnel::AuthStatus::OK;
}

// ------------------------------------------------------------
// Add/update known_hosts
// ------------------------------------------------------------

ssh_tunnel::AuthStatus ssh_tunnel::add_host_key(std::wstring_view known_hosts_path,
                        const ssh_tunnel::HostKeyEntry& entry)
{
    std::wstring full_path;
    if (known_hosts_path.empty()) {
        wchar_t* up = nullptr;
        size_t sz = 0;
        errno_t err = _wdupenv_s(&up, &sz, L"USERPROFILE");
        if (err == 0 && up != nullptr) {
            full_path = up;
            full_path += L"\\.ssh\\known_hosts";
            LocalFree(up);
        }
    } else {
        full_path = std::wstring(known_hosts_path);
    }
    if (full_path.empty())
        return ssh_tunnel::AuthStatus::FAIL;

    std::string keytype_str;
    switch (entry.type) {
    case ssh_tunnel::HostKeyType::RSA: keytype_str = "ssh-rsa"; break;
    case ssh_tunnel::HostKeyType::ECDSA: keytype_str = "ecdsa-sha2-nistp256"; break;
    case ssh_tunnel::HostKeyType::ED25519: keytype_str = "ssh-ed25519"; break;
    default: keytype_str = "ssh-rsa"; break;
    }

    std::string line = std::string(entry.hostname.begin(), entry.hostname.end());
    if (entry.port != 22) {
        line = "[" + line + "]:" + std::to_string(entry.port);
    }
    line += " " + keytype_str + " " + entry.public_key_blob + "\n";

    HANDLE h = CreateFileW(full_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return ssh_tunnel::AuthStatus::FAIL;

    DWORD written = 0;
    BOOL ok = WriteFile(h, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(h);
    return ok ? ssh_tunnel::AuthStatus::OK : ssh_tunnel::AuthStatus::FAIL;
}

ssh_tunnel::AuthStatus ssh_tunnel::update_host_key(std::wstring_view known_hosts_path,
                           std::wstring_view hostname, uint16_t port,
                           std::string_view new_fingerprint_sha256,
                           ssh_tunnel::HostKeyType new_type)
{
    (void)known_hosts_path;
    (void)hostname;
    (void)port;
    (void)new_fingerprint_sha256;
    (void)new_type;
    return ssh_tunnel::AuthStatus::OK;
}

// ------------------------------------------------------------
// Onboarding console
// ------------------------------------------------------------

static void print_wide(const std::wstring& s) {
    DWORD written;
    WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), s.c_str(),
                  static_cast<DWORD>(s.size()), &written, nullptr);
}

static std::wstring read_wide_line() {
    std::wstring line;
    wchar_t ch;
    while (true) {
        DWORD read;
        if (!ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE), &ch, 1, &read, nullptr) || read == 0)
            break;
        if (ch == L'\r' || ch == L'\n') break;
        if (ch == L'\b' || ch == 127) {
            if (!line.empty()) line.pop_back();
        } else {
            line.push_back(ch);
        }
    }
    return line;
}

ssh_tunnel::OnboardingResult ssh_tunnel::run_onboarding_console(std::wstring_view hostname,
                                        uint16_t port,
                                        ssh_tunnel::HostKeyType key_type,
                                        std::string_view fingerprint_sha256,
                                        std::atomic<bool>* cancel_requested)
{
    ssh_tunnel::OnboardingResult result;
    result.status = ssh_tunnel::AuthStatus::FAIL;
    result.fingerprint = std::string(fingerprint_sha256);
    result.hostname = std::wstring(hostname);
    result.port = port;

    std::wstring msg = L"\n=== SSH Host Key Verification ===\n";
    msg += L"Host: " + std::wstring(hostname) + L":" + std::to_wstring(port) + L"\n";
    msg += L"Key type: ";
    switch (key_type) {
        case ssh_tunnel::HostKeyType::RSA: msg += L"RSA"; break;
        case ssh_tunnel::HostKeyType::ECDSA: msg += L"ECDSA"; break;
        case ssh_tunnel::HostKeyType::ED25519: msg += L"ED25519"; break;
        default: msg += L"UNKNOWN"; break;
    }
    msg += L"\nFingerprint (SHA256): ";
    msg += std::wstring(fingerprint_sha256.begin(), fingerprint_sha256.end());
    msg += L"\n\n";
    msg += L"Verify this fingerprint via a trusted channel (e.g. admin, known_hosts file).\n";
    msg += L"Type 'yes' to accept and store, 'no' to cancel: ";
    print_wide(msg);

    std::wstring input = read_wide_line();
    while (!input.empty() && std::isspace(input.back())) input.pop_back();
    while (!input.empty() && std::isspace(input.front())) input.erase(0,1);

    if (cancel_requested && cancel_requested->load()) {
        result.status = ssh_tunnel::AuthStatus::CANCELLED;
        return result;
    }

    std::wstring lower = input;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](wchar_t c) { return std::tolower(c); });

    if (lower == L"yes") {
        result.status = ssh_tunnel::AuthStatus::OK;
    } else {
        result.status = ssh_tunnel::AuthStatus::CANCELLED;
    }
    return result;
}

ssh_tunnel::OnboardingResult ssh_tunnel::run_changed_key_console(std::wstring_view hostname,
                                         uint16_t port,
                                         ssh_tunnel::HostKeyType key_type,
                                         std::string_view old_fingerprint,
                                         std::string_view new_fingerprint,
                                         std::atomic<bool>* cancel_requested)
{
    ssh_tunnel::OnboardingResult result;
    result.status = ssh_tunnel::AuthStatus::FAIL;
    result.fingerprint = std::string(new_fingerprint);
    result.hostname = std::wstring(hostname);
    result.port = port;

    std::wstring msg = L"\n=== WARNING: SSH Host Key Changed ===\n";
    msg += L"Host: " + std::wstring(hostname) + L":" + std::to_wstring(port) + L"\n";
    msg += L"Key type: ";
    switch (key_type) {
        case ssh_tunnel::HostKeyType::RSA: msg += L"RSA"; break;
        case ssh_tunnel::HostKeyType::ECDSA: msg += L"ECDSA"; break;
        case ssh_tunnel::HostKeyType::ED25519: msg += L"ED25519"; break;
        default: msg += L"UNKNOWN"; break;
    }
    msg += L"\nOLD fingerprint: ";
    msg += std::wstring(old_fingerprint.begin(), old_fingerprint.end());
    msg += L"\nNEW fingerprint: ";
    msg += std::wstring(new_fingerprint.begin(), new_fingerprint.end());
    msg += L"\n\n";
    msg += L"This may indicate a man-in-the-middle attack or legitimate key rotation.\n";
    msg += L"Only proceed if you have verified the new fingerprint via a trusted channel.\n";
    msg += L"Type 'yes' to accept the NEW key, 'no' to cancel: ";
    print_wide(msg);

    std::wstring input = read_wide_line();
    while (!input.empty() && std::isspace(input.back())) input.pop_back();
    while (!input.empty() && std::isspace(input.front())) input.erase(0,1);

    if (cancel_requested && cancel_requested->load()) {
        result.status = ssh_tunnel::AuthStatus::CANCELLED;
        return result;
    }

    std::wstring lower = input;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](wchar_t c) { return std::tolower(c); });

    if (lower == L"yes") {
        result.status = ssh_tunnel::AuthStatus::OK;
    } else {
        result.status = ssh_tunnel::AuthStatus::CANCELLED;
    }
    return result;
}

// ------------------------------------------------------------
// Full preflight orchestration
// ------------------------------------------------------------

ssh_tunnel::PreflightResult ssh_tunnel::run_full_preflight(const ssh_tunnel::SshConfig& config,
                                   const ssh_tunnel::AuthContext& auth_ctx,
                                   std::atomic<bool>* cancel_requested)
{
    ssh_tunnel::PreflightResult result;
    result.status = ssh_tunnel::AuthStatus::FAIL;
    result.hostname = config.hostname.empty() ? config.host_pattern : config.hostname;
    result.port = config.port;
    result.onboarding_completed = false;

    std::wstring ssh_path = L"resources\\openssh\\ssh.exe";

    ssh_tunnel::ArgBuilderFlags flags = ssh_tunnel::ArgBuilderFlags::BATCH_MODE | ssh_tunnel::ArgBuilderFlags::STRICT_KEYS;
    std::vector<std::wstring> args = ssh_tunnel::build_ssh_preflight_args(
        config.hostname.empty() ? config.host_pattern : config.hostname,
        config.user,
        config.port,
        flags
    );

    if (!auth_ctx.identity_file_path().empty()) {
        args = ssh_tunnel::build_ssh_preflight_with_key_args(
            config.hostname.empty() ? config.host_pattern : config.hostname,
            config.user,
            config.port,
            auth_ctx.identity_file_path(),
            flags
        );
    }

    ssh_tunnel::ProcessConfig proc_cfg;
    proc_cfg.executable = ssh_path;
    proc_cfg.arguments = args;
    proc_cfg.timeoutMs = 15000;
    proc_cfg.stdoutLimitBytes = 1024 * 1024;
    proc_cfg.stderrLimitBytes = 256 * 1024;

    ssh_tunnel::ProcessResult proc_result = ssh_tunnel::RunSshProcess(proc_cfg, cancel_requested);

    result.status = ssh_tunnel::ssh_exit_to_auth_status(static_cast<int>(proc_result.exitCode));

    if (result.status == ssh_tunnel::AuthStatus::OK || result.status == ssh_tunnel::AuthStatus::HOST_KEY_UNKNOWN ||
        result.status == ssh_tunnel::AuthStatus::HOST_KEY_CHANGED) {
        ssh_tunnel::HostKeyType remote_type = ssh_tunnel::HostKeyType::UNKNOWN;
        if (extract_fingerprint_from_ssh_stderr(proc_result.stderrData,
                                                 result.remote_fingerprint,
                                                 remote_type)) {
            result.remote_type = remote_type;

            auto known = ssh_tunnel::load_known_hosts(L"");
            result.status = ssh_tunnel::verify_host_key(known, result.hostname, result.port,
                                            result.remote_fingerprint, remote_type);
        }
    }

    if (result.status == ssh_tunnel::AuthStatus::HOST_KEY_UNKNOWN) {
        ssh_tunnel::OnboardingResult ob = ssh_tunnel::run_onboarding_console(result.hostname, result.port,
                                                      result.remote_type,
                                                      result.remote_fingerprint,
                                                      cancel_requested);
        if (ob.status == ssh_tunnel::AuthStatus::OK) {
            ssh_tunnel::HostKeyEntry entry;
            entry.hostname = result.hostname;
            entry.port = result.port;
            entry.type = result.remote_type;
            entry.fingerprint_sha256 = result.remote_fingerprint;
            entry.public_key_blob = "";
            entry.verified = true;
            ssh_tunnel::add_host_key(L"", entry);

            result.onboarding_completed = true;
            result.status = ssh_tunnel::AuthStatus::OK;
        } else {
            result.status = ssh_tunnel::AuthStatus::CANCELLED;
        }
    }
    else if (result.status == ssh_tunnel::AuthStatus::HOST_KEY_CHANGED) {
        auto known = ssh_tunnel::load_known_hosts(L"");
        const ssh_tunnel::HostKeyEntry* old_entry = ssh_tunnel::find_host_key(known, result.hostname, result.port);
        std::string old_fp = old_entry ? old_entry->fingerprint_sha256 : "";

        ssh_tunnel::OnboardingResult ob = ssh_tunnel::run_changed_key_console(result.hostname, result.port,
                                                      result.remote_type,
                                                      old_fp,
                                                      result.remote_fingerprint,
                                                      cancel_requested);
        if (ob.status == ssh_tunnel::AuthStatus::OK) {
            ssh_tunnel::update_host_key(L"", result.hostname, result.port,
                            result.remote_fingerprint, result.remote_type);
            result.onboarding_completed = true;
            result.status = ssh_tunnel::AuthStatus::OK;
        } else {
            result.status = ssh_tunnel::AuthStatus::CANCELLED;
        }
    }

    return result;
}