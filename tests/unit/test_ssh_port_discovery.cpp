#include "ssh_port_discovery.h"
#include <cassert>
#include <string>
#include <vector>

using namespace ssh_tunnel;

void test_parse_ss_output_tcp() {
    std::string ss_output = 
        "tcp   LISTEN 0      128          0.0.0.0:22        0.0.0.0:*    users:((\"sshd\",pid=1234,fd=3))\n"
        "tcp   LISTEN 0      128          127.0.0.1:3306     0.0.0.0:*    users:((\"mysqld\",pid=5678,fd=20))\n"
        "tcp   LISTEN 0      50           0.0.0.0:8080       0.0.0.0:*    users:((\"nginx\",pid=9999,fd=4))\n";
    
    auto ports = parse_ss_output(ss_output, TransportProtocol::TCP);
    
    assert(ports.size() == 3);
    
    // Check first entry (sshd)
    assert(ports[0].protocol == TransportProtocol::TCP);
    assert(ports[0].local_address == "0.0.0.0");
    assert(ports[0].local_port == 22);
    assert(ports[0].process_name == "sshd");
    assert(ports[0].pid == 1234);
    
    // Check second entry (mysqld)
    assert(ports[1].local_address == "127.0.0.1");
    assert(ports[1].local_port == 3306);
    assert(ports[1].process_name == "mysqld");
    assert(ports[1].pid == 5678);
    
    // Check third entry (nginx)
    assert(ports[2].local_address == "0.0.0.0");
    assert(ports[2].local_port == 8080);
    assert(ports[2].process_name == "nginx");
    assert(ports[2].pid == 9999);
}

void test_parse_ss_output_udp() {
    std::string ss_output = 
        "udp   UNCONN 0      0            0.0.0.0:68         0.0.0.0:*    users:((\"dhclient\",pid=111,fd=6))\n"
        "udp   UNCONN 0      0            127.0.0.1:53        0.0.0.0:*    users:((\"dnsmasq\",pid=222,fd=4))\n";
    
    auto ports = parse_ss_output(ss_output, TransportProtocol::UDP);
    
    assert(ports.size() == 2);
    assert(ports[0].protocol == TransportProtocol::UDP);
    assert(ports[0].local_port == 68);
    assert(ports[0].process_name == "dhclient");
    assert(ports[1].local_port == 53);
    assert(ports[1].process_name == "dnsmasq");
}

void test_parse_ss_output_filter_tcp() {
    std::string ss_output = 
        "tcp   LISTEN 0      128          0.0.0.0:22        0.0.0.0:*    users:((\"sshd\",pid=1234,fd=3))\n"
        "udp   UNCONN 0      0            0.0.0.0:68         0.0.0.0:*    users:((\"dhclient\",pid=111,fd=6))\n";
    
    auto ports = parse_ss_output(ss_output, TransportProtocol::TCP);
    assert(ports.size() == 1);
    assert(ports[0].protocol == TransportProtocol::TCP);
    assert(ports[0].local_port == 22);
}

void test_parse_ss_output_filter_udp() {
    std::string ss_output = 
        "tcp   LISTEN 0      128          0.0.0.0:22        0.0.0.0:*    users:((\"sshd\",pid=1234,fd=3))\n"
        "udp   UNCONN 0      0            0.0.0.0:68         0.0.0.0:*    users:((\"dhclient\",pid=111,fd=6))\n";
    
    auto ports = parse_ss_output(ss_output, TransportProtocol::UDP);
    assert(ports.size() == 1);
    assert(ports[0].protocol == TransportProtocol::UDP);
}

void test_parse_ss_output_empty() {
    std::string ss_output = "";
    auto ports = parse_ss_output(ss_output);
    assert(ports.empty());
}

void test_parse_ss_output_no_users() {
    std::string ss_output = 
        "tcp   LISTEN 0      128          0.0.0.0:22        0.0.0.0:*\n";
    
    auto ports = parse_ss_output(ss_output, TransportProtocol::TCP);
    // Should handle gracefully even without users field
}

void test_parse_ss_output_ipv6() {
    std::string ss_output = 
        "tcp   LISTEN 0      128             [::]:22           [::]:*    users:((\"sshd\",pid=1234,fd=3))\n"
        "tcp   LISTEN 0      128          0.0.0.0:22        0.0.0.0:*    users:((\"sshd\",pid=1234,fd=3))\n";
    
    auto ports = parse_ss_output(ss_output, TransportProtocol::TCP);
    assert(ports.size() == 2);
    assert(ports[0].local_address == "::");
    assert(ports[0].local_port == 22);
    assert(ports[1].local_address == "0.0.0.0");
}

void test_parse_ss_output_with_port_in_brackets() {
    std::string ss_output = 
        "tcp   LISTEN 0      128       [::1]:8080         [::]:*    users:((\"app\",pid=1234,fd=3))\n";
    
    auto ports = parse_ss_output(ss_output, TransportProtocol::TCP);
    assert(ports.size() == 1);
    assert(ports[0].local_address == "::1");
    assert(ports[0].local_port == 8080);
}

void test_transport_protocol_values() {
    assert(static_cast<int>(TransportProtocol::UNKNOWN) == 0);
    assert(static_cast<int>(TransportProtocol::TCP) == 1);
    assert(static_cast<int>(TransportProtocol::UDP) == 2);
}

void test_listening_port_struct() {
    ListeningPort lp;
    lp.protocol = TransportProtocol::TCP;
    lp.local_address = "127.0.0.1";
    lp.local_port = 8080;
    lp.process_name = "test";
    lp.pid = 1234;
    lp.user = "user";
    lp.raw_line = "raw";
    
    assert(lp.protocol == TransportProtocol::TCP);
    assert(lp.local_port == 8080);
    assert(lp.pid == 1234);
}

void test_local_port_info_struct() {
    LocalPortInfo lpi;
    lpi.protocol = TransportProtocol::UDP;
    lpi.local_address = "0.0.0.0";
    lpi.local_port = 53;
    lpi.process_name = "dns";
    lpi.pid = 5678;
    
    assert(lpi.protocol == TransportProtocol::UDP);
    assert(lpi.local_port == 53);
    assert(lpi.pid == 5678);
}

int main() {
    test_parse_ss_output_tcp();
    test_parse_ss_output_udp();
    test_parse_ss_output_filter_tcp();
    test_parse_ss_output_filter_udp();
    test_parse_ss_output_empty();
    test_parse_ss_output_no_users();
    test_parse_ss_output_ipv6();
    test_parse_ss_output_with_port_in_brackets();
    test_transport_protocol_values();
    test_listening_port_struct();
    test_local_port_info_struct();
    return 0;
}