#include "ssh_auth.h"
#include "ssh_process.h"
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>

using namespace ssh_tunnel;

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

static std::wstring resolve_userprofile_ssh_config()
{
    wchar_t* up = nullptr;
    size_t sz = 0;
    errno_t err = _wdupenv_s(&up, &sz, L"USERPROFILE");
    if (err != 0 || up == nullptr) {
        return L"";
    }
    std::wstring result = up;
    result += L"\\.ssh\\config";
    LocalFree(up);
    return result;
}

// ------------------------------------------------------------
// SshConfig::parse_ssh_config
// ------------------------------------------------------------

AuthStatus ssh_tunnel::parse_ssh_config(std::wstring_view config_path, SshConfig& out)
{
    if (config_path.empty()) {
        out.host_pattern = L"*";
        out.hostname = L"";
        out.port = 22;
        out.user = L"";
        out.identity_file = L"";
        out.proxy_jump = false;
        return AuthStatus::OK;
    }

    HANDLE h = CreateFileW(
        config_path.data(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        out.host_pattern = L"*";
        out.hostname = L"";
        out.port = 22;
        out.user = L"";
        out.identity_file = L"";
        out.proxy_jump = false;
        return AuthStatus::OK;
    }

    DWORD file_size = GetFileSize(h, nullptr);
    std::vector<char> buf(file_size + 1);
    DWORD nread = 0;
    ReadFile(h, buf.data(), file_size, &nread, nullptr);
    buf[nread] = '\0';
    CloseHandle(h);

    int wlen = MultiByteToWideChar(CP_UTF8, 0, buf.data(), nread, nullptr, 0);
    std::wstring wbuf(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, buf.data(), nread, &wbuf[0], wlen);

    out.host_pattern = L"*";
    out.hostname = L"";
    out.port = 22;
    out.user = L"";
    out.identity_file = L"";
    out.proxy_jump = false;

    size_t pos = 0;
    while (pos < wbuf.size()) {
        size_t eol = wbuf.find(L"\n", pos);
        if (eol == std::wstring::npos) eol = wbuf.size();
        std::wstring line = wbuf.substr(pos, eol - pos);
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();

        if (line.empty() || line[0] == L'#') {
            pos = eol + 1;
            continue;
        }

        size_t eq = line.find(L' ');
        if (eq != std::wstring::npos) {
            std::wstring key = line.substr(0, eq);
            std::wstring value = line.substr(eq + 1);

            std::wstring lower_key = key;
            for (auto& ch : lower_key) ch = std::tolower(ch);

            if (lower_key == L"hostname") {
                out.hostname = value;
            }
            else if (lower_key == L"port") {
                try {
                    int p = std::stoi(std::string(value.begin(), value.end()));
                    if (p >= 1 && p <= 65535)
                        out.port = static_cast<uint16_t>(p);
                }
                catch (...) {}
            }
            else if (lower_key == L"user") {
                out.user = value;
            }
            else if (lower_key == L"identityfile") {
                out.identity_file = value;
            }
            else if (lower_key == L"proxyjump" || lower_key == L"proxy jump") {
                out.proxy_jump = true;
            }
        }
        else {
            if (out.host_pattern == L"*") {
                out.host_pattern = line;
            }
        }

        pos = eol + 1;
    }

    if (out.host_pattern.empty()) {
        out.host_pattern = L"*";
    }

    return AuthStatus::OK;
}

// ------------------------------------------------------------
// AuthContext
// ------------------------------------------------------------

AuthContext::AuthContext()
    : agent_available_(false), key_loaded_(false), passphrase_required_(false)
{
}

AuthContext::~AuthContext()
{
}

AuthStatus AuthContext::load_key(std::wstring_view key_path)
{
    if (key_path.empty()) {
        key_loaded_ = false;
        passphrase_required_ = false;
        identity_file_path_.clear();
        return AuthStatus::OK;
    }

    DWORD attr = GetFileAttributesW(reinterpret_cast<LPCWSTR>(key_path.data()));
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return AuthStatus::KEY_NOT_LOADED;
    }

    HANDLE h = CreateFileW(
        reinterpret_cast<LPCWSTR>(key_path.data()),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return AuthStatus::KEY_NOT_LOADED;
    }

    char header[256] = {0};
    DWORD nread = 0;
    ReadFile(h, header, sizeof(header) - 1, &nread, nullptr);
    CloseHandle(h);

    std::string header_str(header, nread);
    bool encrypted = (header_str.find("ENCRYPTED") != std::string::npos) ||
                     (header_str.find("BEGIN RSA PRIVATE KEY") != std::string::npos &&
                      header_str.find("DES") != std::string::npos);

    if (encrypted) {
        if (agent_available_) {
            key_loaded_ = true;
            passphrase_required_ = false;
            identity_file_path_ = std::wstring(key_path);
            return AuthStatus::OK;
        }
        else {
            key_loaded_ = false;
            passphrase_required_ = true;
            identity_file_path_ = std::wstring(key_path);
            return AuthStatus::KEY_PASSPHRASE_REQUIRED;
        }
    }
    else {
        key_loaded_ = true;
        passphrase_required_ = false;
        identity_file_path_ = std::wstring(key_path);
        return AuthStatus::OK;
    }
}

void AuthContext::set_agent_available(bool available)
{
    agent_available_ = available;
}

bool AuthContext::key_loaded() const noexcept
{
    return key_loaded_;
}

bool AuthContext::passphrase_required() const noexcept
{
    return passphrase_required_;
}

std::wstring AuthContext::identity_file_path() const noexcept
{
    return identity_file_path_;
}

// ------------------------------------------------------------
// Host alias extraction
// ------------------------------------------------------------

std::wstring ssh_tunnel::extract_host_alias(std::wstring_view arg)
{
    size_t start = 0;
    const size_t at = arg.find(L'@');
    if (at != std::wstring::npos) {
        start = at + 1;
    }

    std::wstring_view host_port = arg.substr(start);
    if (host_port.empty()) {
        return std::wstring(host_port);
    }

    // Bracketed IPv6 literal: [::1]:22 -> [::1]
    if (host_port.front() == L'[') {
        const size_t close = host_port.find(L']');
        if (close != std::wstring_view::npos) {
            return std::wstring(host_port.substr(0, close + 1));
        }
    }

    const size_t colon = host_port.rfind(L':');
    if (colon != std::wstring_view::npos && colon > 0) {
        const std::wstring port_part(host_port.substr(colon + 1));
        try {
            const int p = std::stoi(std::string(port_part.begin(), port_part.end()));
            if (p >= 1 && p <= 65535) {
                return std::wstring(host_port.substr(0, colon));
            }
        }
        catch (...) {}
    }
    return std::wstring(host_port);
}

// ------------------------------------------------------------
// ssh_exit_to_auth_status
// ------------------------------------------------------------

ssh_tunnel::AuthStatus ssh_tunnel::ssh_exit_to_auth_status(int exit_code)
{
    switch (exit_code) {
    case 0:  return ssh_tunnel::AuthStatus::OK;
    case 1:  return ssh_tunnel::AuthStatus::FAIL;
    case 2:  return ssh_tunnel::AuthStatus::FAIL;
    case 3:  return ssh_tunnel::AuthStatus::FAIL;
    case 126: return ssh_tunnel::AuthStatus::FAIL;
    case 127: return ssh_tunnel::AuthStatus::FAIL;
    default: return ssh_tunnel::AuthStatus::FAIL;
    }
}