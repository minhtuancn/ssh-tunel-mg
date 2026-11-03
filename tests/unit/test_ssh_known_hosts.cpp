#include "ssh_known_hosts.h"
#include <cassert>
#include <string>
#include <vector>

using namespace ssh_tunnel;

void test_parse_known_hosts_line_rsa() {
    std::string line = "example.com ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQC...";
    HostKeyEntry entry;
    bool result = parse_known_hosts_line(line, entry);
    assert(result);
    assert(entry.hostname == L"example.com");
    assert(entry.port == 22);
    assert(entry.type == HostKeyType::RSA);
    assert(entry.public_key_blob == "AAAAB3NzaC1yc2EAAAADAQABAAABAQC...");
}

void test_parse_known_hosts_line_ecdsa() {
    std::string line = "example.com ecdsa-sha2-nistp256 AAAAE2VjZHNhLXNoYTItbmlzdHAyNTYAAAAIbmlzdHAyNTYAAABBBB...";
    HostKeyEntry entry;
    bool result = parse_known_hosts_line(line, entry);
    assert(result);
    assert(entry.hostname == L"example.com");
    assert(entry.type == HostKeyType::ECDSA);
}

void test_parse_known_hosts_line_ed25519() {
    std::string line = "example.com ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAII...";
    HostKeyEntry entry;
    bool result = parse_known_hosts_line(line, entry);
    assert(result);
    assert(entry.hostname == L"example.com");
    assert(entry.type == HostKeyType::ED25519);
}

void test_parse_known_hosts_line_with_port() {
    std::string line = "[example.com]:2222 ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQC...";
    HostKeyEntry entry;
    bool result = parse_known_hosts_line(line, entry);
    assert(result);
    assert(entry.hostname == L"example.com");
    assert(entry.port == 2222);
}

void test_parse_known_hosts_line_multiple_hosts() {
    std::string line = "host1,host2,host3 ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQC...";
    HostKeyEntry entry;
    bool result = parse_known_hosts_line(line, entry);
    assert(result);
    // Should pick first non-hashed host
    assert(entry.hostname == L"host1");
}

void test_parse_known_hosts_line_hashed() {
    std::string line = "|1|hash1|hash2| ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQC...";
    HostKeyEntry entry;
    bool result = parse_known_hosts_line(line, entry);
    // Hashed entries are skipped
    assert(!result);
}

void test_parse_known_hosts_line_comment() {
    std::string line = "# This is a comment";
    HostKeyEntry entry;
    bool result = parse_known_hosts_line(line, entry);
    assert(!result);
}

void test_find_host_key() {
    std::vector<HostKeyEntry> entries;
    HostKeyEntry e1;
    e1.hostname = L"host1";
    e1.port = 22;
    entries.push_back(e1);
    
    HostKeyEntry e2;
    e2.hostname = L"host2";
    e2.port = 2222;
    entries.push_back(e2);
    
    const HostKeyEntry* found = find_host_key(entries, L"host1", 22);
    assert(found != nullptr);
    assert(found->hostname == L"host1");
    
    found = find_host_key(entries, L"host2", 2222);
    assert(found != nullptr);
    assert(found->port == 2222);
    
    found = find_host_key(entries, L"host3", 22);
    assert(found == nullptr);
    
    found = find_host_key(entries, L"host1", 2222);
    assert(found == nullptr);
}

void test_verify_host_key_unknown() {
    std::vector<HostKeyEntry> known;
    auto result = verify_host_key(known, L"newhost", 22, "SHA256:abc123", HostKeyType::RSA);
    assert(result == AuthStatus::HOST_KEY_UNKNOWN);
}

void test_verify_host_key_changed() {
    std::vector<HostKeyEntry> known;
    HostKeyEntry e;
    e.hostname = L"host";
    e.port = 22;
    e.fingerprint_sha256 = "SHA256:old";
    e.verified = true;
    known.push_back(e);
    
    auto result = verify_host_key(known, L"host", 22, "SHA256:new", HostKeyType::RSA);
    assert(result == AuthStatus::HOST_KEY_CHANGED);
}

void test_verify_host_key_ok() {
    std::vector<HostKeyEntry> known;
    HostKeyEntry e;
    e.hostname = L"host";
    e.port = 22;
    e.fingerprint_sha256 = "SHA256:same";
    e.verified = true;
    known.push_back(e);
    
    auto result = verify_host_key(known, L"host", 22, "SHA256:same", HostKeyType::RSA);
    assert(result == AuthStatus::OK);
}

void test_verify_host_key_unverified() {
    std::vector<HostKeyEntry> known;
    HostKeyEntry e;
    e.hostname = L"host";
    e.port = 22;
    e.fingerprint_sha256 = "SHA256:same";
    e.verified = false;  // Not verified
    known.push_back(e);
    
    auto result = verify_host_key(known, L"host", 22, "SHA256:same", HostKeyType::RSA);
    // Unverified entries treated as unknown for onboarding
    assert(result == AuthStatus::HOST_KEY_UNKNOWN);
}

int main() {
    test_parse_known_hosts_line_rsa();
    test_parse_known_hosts_line_ecdsa();
    test_parse_known_hosts_line_ed25519();
    test_parse_known_hosts_line_with_port();
    test_parse_known_hosts_line_multiple_hosts();
    test_parse_known_hosts_line_hashed();
    test_parse_known_hosts_line_comment();
    test_find_host_key();
    test_verify_host_key_unknown();
    test_verify_host_key_changed();
    test_verify_host_key_ok();
    test_verify_host_key_unverified();
    return 0;
}