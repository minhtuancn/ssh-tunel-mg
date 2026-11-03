#include "ssh_arguments.h"
#include <algorithm>
#include <cassert>
#include <initializer_list>
#include <string>
#include <vector>

using namespace ssh_tunnel;

void test_arg_builder_flags() {
    // Test bitwise operators
    ArgBuilderFlags f1 = ArgBuilderFlags::BATCH_MODE | ArgBuilderFlags::STRICT_KEYS;
    assert(static_cast<int>(f1) == (1 | 2));
    
    ArgBuilderFlags f2 = f1 | ArgBuilderFlags::AGENT_FORWARD;
    assert(static_cast<int>(f2) == (1 | 2 | 8));
    
    f1 |= ArgBuilderFlags::AGENT_FORWARD;
    assert(static_cast<int>(f1) == (1 | 2 | 8));
}

void test_contains_shell_metacharacters() {
    // These should be detected as dangerous
    assert(ArgBuilder::contains_shell_metacharacters(L"test&"));
    assert(ArgBuilder::contains_shell_metacharacters(L"test|"));
    assert(ArgBuilder::contains_shell_metacharacters(L"test;"));
    assert(ArgBuilder::contains_shell_metacharacters(L"test("));
    assert(ArgBuilder::contains_shell_metacharacters(L"test)"));
    assert(ArgBuilder::contains_shell_metacharacters(L"test<"));
    assert(ArgBuilder::contains_shell_metacharacters(L"test>"));
    assert(ArgBuilder::contains_shell_metacharacters(L"test$"));
    assert(ArgBuilder::contains_shell_metacharacters(L"test`"));
    assert(ArgBuilder::contains_shell_metacharacters(L"test\""));
    assert(ArgBuilder::contains_shell_metacharacters(L"test'"));
    assert(ArgBuilder::contains_shell_metacharacters(L"test\\"));
    assert(ArgBuilder::contains_shell_metacharacters(L"test*"));
    assert(ArgBuilder::contains_shell_metacharacters(L"test?"));
    assert(ArgBuilder::contains_shell_metacharacters(L"test["));
    assert(ArgBuilder::contains_shell_metacharacters(L"test]"));
    assert(ArgBuilder::contains_shell_metacharacters(L"test{"));
    assert(ArgBuilder::contains_shell_metacharacters(L"test}"));
    assert(ArgBuilder::contains_shell_metacharacters(L"test~"));
    assert(ArgBuilder::contains_shell_metacharacters(L"test\n"));
    assert(ArgBuilder::contains_shell_metacharacters(L"test\r"));
    assert(ArgBuilder::contains_shell_metacharacters(L"test\t"));
    
    // These should be safe
    assert(!ArgBuilder::contains_shell_metacharacters(L"safe"));
    assert(!ArgBuilder::contains_shell_metacharacters(L"user@host"));
    assert(!ArgBuilder::contains_shell_metacharacters(L"192.168.1.1"));
    assert(!ArgBuilder::contains_shell_metacharacters(L"path/to/file"));
    assert(!ArgBuilder::contains_shell_metacharacters(L"-i"));
    assert(!ArgBuilder::contains_shell_metacharacters(L"-p"));
    assert(!ArgBuilder::contains_shell_metacharacters(L"22"));
    assert(!ArgBuilder::contains_shell_metacharacters(L"BatchMode=yes"));
}

void test_arg_builder_basic() {
    ArgBuilder builder(ArgBuilderFlags::BATCH_MODE | ArgBuilderFlags::STRICT_KEYS);
    builder.set_host(L"example.com");
    builder.set_user(L"testuser");
    builder.set_port(2222);
    
    auto args = builder.build();
    assert(!args.empty());
    assert(args[0] == L"ssh");
    
    // Check for -o BatchMode=yes
    bool found_batch = false;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == L"-o" && i + 1 < args.size() && args[i + 1] == L"BatchMode=yes") {
            found_batch = true;
            break;
        }
    }
    assert(found_batch);
    
    // Check for -p 2222
    bool found_port = false;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == L"-p" && i + 1 < args.size() && args[i + 1] == L"2222") {
            found_port = true;
            break;
        }
    }
    assert(found_port);
    
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

void test_arg_builder_with_key() {
    ArgBuilder builder(ArgBuilderFlags::BATCH_MODE);
    builder.set_host(L"example.com");
    builder.set_user(L"testuser");
    builder.set_identity_file(L"/home/user/.ssh/id_rsa");
    
    auto args = builder.build();
    
    bool found_key = false;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == L"-i" && i + 1 < args.size() && args[i + 1] == L"/home/user/.ssh/id_rsa") {
            found_key = true;
            break;
        }
    }
    assert(found_key);
}

void test_arg_builder_remote_command() {
    ArgBuilder builder(ArgBuilderFlags::BATCH_MODE);
    builder.set_host(L"example.com");
    builder.set_user(L"testuser");
    builder.set_remote_command(L"ls -la");
    
    auto args = builder.build();
    
    // Remote command should be last argument
    assert(args.back() == L"ls -la");
}

void test_arg_builder_strict_host_key_checking() {
    ArgBuilder strict(ArgBuilderFlags::BATCH_MODE | ArgBuilderFlags::STRICT_KEYS);
    strict.set_host(L"example.com");
    auto args = strict.build();

    bool found_strict = false;
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == L"-o" && args[i + 1] == L"StrictHostKeyChecking=yes") {
            found_strict = true;
            break;
        }
    }
    assert(found_strict);

    // Without STRICT_KEYS the option must not be added implicitly.
    ArgBuilder relaxed(ArgBuilderFlags::BATCH_MODE);
    relaxed.set_host(L"example.com");
    auto relaxed_args = relaxed.build();
    for (const auto& arg : relaxed_args) {
        assert(arg != L"StrictHostKeyChecking=yes");
    }
}

void test_arg_builder_add_option() {
    ArgBuilder builder(ArgBuilderFlags::NONE);
    builder.set_host(L"example.com");
    builder.add_option(L"BatchMode=yes");
    builder.add_option(L"LogLevel=ERROR");

    // add_option must survive build() (build used to clear the option list).
    auto args = builder.build();
    auto contains = [&args](const std::wstring& needle) {
        return std::find(args.begin(), args.end(), needle) != args.end();
    };
    assert(contains(L"-o"));
    assert(contains(L"BatchMode=yes"));
    assert(contains(L"LogLevel=ERROR"));

    // Rebuilding must still emit the options.
    auto again = builder.build();
    assert(std::find(again.begin(), again.end(), L"BatchMode=yes") != again.end());

    // Options containing shell metacharacters are rejected.
    ArgBuilder unsafe(ArgBuilderFlags::NONE);
    unsafe.set_host(L"example.com");
    unsafe.add_option(L"ProxyCommand=calc.exe & whoami");
    auto unsafe_args = unsafe.build();
    for (const auto& arg : unsafe_args) {
        assert(arg.find(L"whoami") == std::wstring::npos);
    }
}

void test_preflight_enforces_strict_and_batch() {
    for (auto args : {build_ssh_preflight_args(L"example.com", L"testuser", 22),
                      build_ssh_preflight_with_key_args(L"example.com", L"testuser", 22, L"/path/key")}) {
        bool strict = false;
        bool batch = false;
        for (size_t i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == L"-o" && args[i + 1] == L"StrictHostKeyChecking=yes") strict = true;
            if (args[i] == L"-o" && args[i + 1] == L"BatchMode=yes") batch = true;
        }
        assert(strict);
        assert(batch);
    }
}

void test_build_ssh_preflight_args() {
    auto args = build_ssh_preflight_args(L"example.com", L"testuser", 2222);
    
    assert(!args.empty());
    assert(args[0] == L"ssh");
    
    bool found_batch = false;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == L"-o" && i + 1 < args.size() && args[i + 1] == L"BatchMode=yes") {
            found_batch = true;
            break;
        }
    }
    assert(found_batch);
}

void test_build_ssh_preflight_with_key_args() {
    auto args = build_ssh_preflight_with_key_args(L"example.com", L"testuser", 2222, L"/path/key");
    
    bool found_key = false;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == L"-i" && i + 1 < args.size() && args[i + 1] == L"/path/key") {
            found_key = true;
            break;
        }
    }
    assert(found_key);
}

void test_reject_shell_metacharacters() {
    ArgBuilder builder(ArgBuilderFlags::NONE);
    
    // These should be rejected (stored as empty)
    builder.set_host(L"host; rm -rf /");
    builder.set_user(L"user`id`");
    builder.set_identity_file(L"key; cat /etc/passwd");
    builder.set_remote_command(L"ls; cat /etc/shadow");
    
    auto args = builder.build();
    
    // Host should be empty due to rejection
    bool found_empty_host = false;
    for (const auto& arg : args) {
        if (arg.empty() || arg == L"") {
            found_empty_host = true;
        }
    }
    // The host part should be empty or not contain the malicious part
}

int main() {
    test_arg_builder_flags();
    test_contains_shell_metacharacters();
    test_arg_builder_basic();
    test_arg_builder_with_key();
    test_arg_builder_remote_command();
    test_arg_builder_strict_host_key_checking();
    test_arg_builder_add_option();
    test_preflight_enforces_strict_and_batch();
    test_build_ssh_preflight_args();
    test_build_ssh_preflight_with_key_args();
    test_reject_shell_metacharacters();
    return 0;
}