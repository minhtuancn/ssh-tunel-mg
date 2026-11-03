#include "ssh_arguments.h"
#include "ssh_auth.h"
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

using namespace ssh_tunnel;

// ------------------------------------------------------------
// ArgBuilder implementation
// ------------------------------------------------------------

bool ArgBuilder::contains_shell_metacharacters(std::wstring_view s)
{
    for (wchar_t c : s) {
        if (c == L'&' || c == L'|' || c == L';' ||
            c == L'(' || c == L')' || c == L'<' || c == L'>' ||
            c == L'$' || c == L'`' || c == L'"' || c == L'\'' ||
            c == L'\\' || c == L'*' || c == L'?' || c == L'[' ||
            c == L']' || c == L'{' || c == L'}' || c == L'~' ||
            c == L'\n' || c == L'\r' || c == L'\t') {
            return true;
        }
    }
    return false;
}

ArgBuilder::ArgBuilder(ArgBuilderFlags flags)
    : flags_(flags), port_(0)
{
}

void ArgBuilder::set_host(std::wstring_view host)
{
    if (contains_shell_metacharacters(host)) {
        host_.clear();
    }
    else {
        host_ = std::wstring(host);
    }
}

void ArgBuilder::set_user(std::wstring_view user)
{
    if (!user.empty() && !contains_shell_metacharacters(user)) {
        user_ = std::wstring(user);
    }
}

void ArgBuilder::set_port(uint16_t port)
{
    port_ = port;
}

void ArgBuilder::set_identity_file(std::wstring_view path)
{
    if (!path.empty() && !contains_shell_metacharacters(path)) {
        identity_file_ = std::wstring(path);
    }
}

void ArgBuilder::set_remote_command(std::wstring_view cmd)
{
    if (cmd.empty()) {
        remote_command_.clear();
        return;
    }
    if (!contains_shell_metacharacters(cmd)) {
        remote_command_ = std::wstring(cmd);
    }
}

void ArgBuilder::add_option(std::wstring_view option)
{
    if (!option.empty() && !contains_shell_metacharacters(option)) {
        items_.push_back(L"-o");
        items_.push_back(std::wstring(option));
    }
}

std::vector<std::wstring> ArgBuilder::build()
{
    // Options added via add_option() must survive the rebuild.
    std::vector<std::wstring> extra_options = std::move(items_);
    items_.clear();

    items_.push_back(L"ssh");

    if (static_cast<int>(flags_) & static_cast<int>(ArgBuilderFlags::BATCH_MODE)) {
        items_.push_back(L"-o");
        items_.push_back(L"BatchMode=yes");
    }

    if (static_cast<int>(flags_) & static_cast<int>(ArgBuilderFlags::STRICT_KEYS)) {
        items_.push_back(L"-o");
        items_.push_back(L"StrictHostKeyChecking=yes");
    }

    if (static_cast<int>(flags_) & static_cast<int>(ArgBuilderFlags::AGENT_FORWARD)) {
        items_.push_back(L"-o");
        items_.push_back(L"ForwardAgent=yes");
    }

    for (const auto& option : extra_options) {
        items_.push_back(option);
    }

    if (!identity_file_.empty()) {
        items_.push_back(L"-i");
        items_.push_back(identity_file_);
    }

    if (port_ != 0 && port_ >= 1 && port_ <= 65535) {
        std::wstring port_str = std::to_wstring(port_);
        items_.push_back(L"-p");
        items_.push_back(port_str);
    }

    std::wstring host_part;
    if (!user_.empty()) {
        host_part = user_ + L"@" + host_;
    }
    else {
        host_part = host_;
    }
    if (!host_part.empty()) {
        items_.push_back(host_part);
    }

    if (!remote_command_.empty()) {
        items_.push_back(remote_command_);
    }

    return items_;
}

std::vector<std::wstring> ssh_tunnel::build_ssh_preflight_args(
    std::wstring_view host,
    std::wstring_view user,
    uint16_t port,
    ArgBuilderFlags flags)
{
    // Preflight must never prompt and must always verify host keys.
    ArgBuilder builder(flags | ArgBuilderFlags::BATCH_MODE | ArgBuilderFlags::STRICT_KEYS);
    builder.set_host(host);
    if (!user.empty()) builder.set_user(user);
    builder.set_port(port);
    return builder.build();
}

std::vector<std::wstring> ssh_tunnel::build_ssh_preflight_with_key_args(
    std::wstring_view host,
    std::wstring_view user,
    uint16_t port,
    std::wstring_view keyfile,
    ArgBuilderFlags flags)
{
    ArgBuilder builder(flags | ArgBuilderFlags::BATCH_MODE | ArgBuilderFlags::STRICT_KEYS);
    builder.set_host(host);
    if (!user.empty()) builder.set_user(user);
    builder.set_port(port);
    if (!keyfile.empty()) builder.set_identity_file(keyfile);
    return builder.build();
}
