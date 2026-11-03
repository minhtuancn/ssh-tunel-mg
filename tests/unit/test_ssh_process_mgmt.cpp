#include "ssh_process_mgmt.h"
#include <cassert>
#include <string>
#include <vector>
#include <winsock2.h>

using namespace ssh_tunnel;

void test_process_state_values() {
    assert(static_cast<int>(ProcessState::UNKNOWN) == 0);
    assert(static_cast<int>(ProcessState::RUNNING) == 1);
    assert(static_cast<int>(ProcessState::SLEEPING) == 2);
    assert(static_cast<int>(ProcessState::WAITING) == 3);
    assert(static_cast<int>(ProcessState::ZOMBIE) == 4);
    assert(static_cast<int>(ProcessState::STOPPED) == 5);
    assert(static_cast<int>(ProcessState::TRACING) == 6);
    assert(static_cast<int>(ProcessState::DEAD) == 7);
    assert(static_cast<int>(ProcessState::WAKEKILL) == 8);
    assert(static_cast<int>(ProcessState::WAKING) == 9);
    assert(static_cast<int>(ProcessState::PARKED) == 10);
}

void test_kill_signal_values() {
    assert(static_cast<int>(KillSignal::TERM) == 15);
    assert(static_cast<int>(KillSignal::KILL) == 9);
    assert(static_cast<int>(KillSignal::INT) == 2);
    assert(static_cast<int>(KillSignal::HUP) == 1);
    assert(static_cast<int>(KillSignal::QUIT) == 3);
    assert(static_cast<int>(KillSignal::USR1) == 10);
    assert(static_cast<int>(KillSignal::USR2) == 12);
}

void test_parse_ps_output() {
    std::string ps_output = 
        "1234 user1  sshd       sshd: user1@pts/0  S    0.0  0.1  12345  6789  pts/0  Mon Oct 25 10:00:00 2026\n"
        "5678 user2  nginx      nginx: master process  S    0.0  0.5  23456  7890  ?      Mon Oct 25 10:01:00 2026\n"
        "9999 user3  mysqld     /usr/sbin/mysqld  S    0.1  2.0  34567  8901  ?      Mon Oct 25 10:02:00 2026\n";
    
    auto processes = parse_ps_output(ps_output);
    
    assert(processes.size() == 3);
    
    // First process
    assert(processes[0].pid == 1234);
    assert(processes[0].user == "user1");
    assert(processes[0].command == "sshd");
    assert(processes[0].args == "sshd: user1@pts/0");
    assert(processes[0].state == ProcessState::SLEEPING);
    assert(processes[0].cpu_percent == 0.0);
    assert(processes[0].mem_percent == 0.1);
    assert(processes[0].vsz == 12345);
    assert(processes[0].rss == 6789);
    assert(processes[0].tty == "pts/0");
    
    // Second process
    assert(processes[1].pid == 5678);
    assert(processes[1].command == "nginx");
    assert(processes[1].args == "nginx: master process");
    assert(processes[1].mem_percent == 0.5);
    
    // Third process
    assert(processes[2].pid == 9999);
    assert(processes[2].command == "mysqld");
    assert(processes[2].mem_percent == 2.0);
}

void test_parse_ps_output_running() {
    std::string ps_output = 
        "1111 root  app  ./app  R    5.0  1.0  10000  5000  ?  Mon Oct 25 10:00:00 2026\n";
    
    auto processes = parse_ps_output(ps_output);
    assert(processes.size() == 1);
    assert(processes[0].state == ProcessState::RUNNING);
    assert(processes[0].cpu_percent == 5.0);
}

void test_parse_ps_output_zombie() {
    std::string ps_output = 
        "2222 user  zombie  zombie_process  Z    0.0  0.0  0  0  ?  Mon Oct 25 10:00:00 2026\n";
    
    auto processes = parse_ps_output(ps_output);
    assert(processes.size() == 1);
    assert(processes[0].state == ProcessState::ZOMBIE);
}

void test_parse_ps_output_stopped() {
    std::string ps_output = 
        "3333 user  stopped  ./stopped  T    0.0  0.0  5000  1000  pts/1  Mon Oct 25 10:00:00 2026\n";
    
    auto processes = parse_ps_output(ps_output);
    assert(processes.size() == 1);
    assert(processes[0].state == ProcessState::STOPPED);
}

void test_parse_ps_output_empty() {
    std::string ps_output = "";
    auto processes = parse_ps_output(ps_output);
    assert(processes.empty());
}

void test_parse_ps_output_with_special_chars_in_args() {
    std::string ps_output = 
        "4444 user  script  /path/script.sh arg1 \"arg with spaces\" arg3  S    0.0  0.1  1000  500  ?  Mon Oct 25 10:00:00 2026\n";
    
    auto processes = parse_ps_output(ps_output);
    assert(processes.size() == 1);
    // Args should contain the full command line
    assert(processes[0].args.find("arg1") != std::string::npos);
    assert(processes[0].args.find("arg with spaces") != std::string::npos);
}

void test_kill_result_struct() {
    KillResult kr;
    kr.success = true;
    kr.exit_code = 0;
    kr.error_message = "";
    
    assert(kr.success == true);
    assert(kr.exit_code == 0);
    
    KillResult kr2;
    kr2.success = false;
    kr2.exit_code = 1;
    kr2.error_message = "Permission denied";
    
    assert(kr2.success == false);
    assert(kr2.exit_code == 1);
    assert(kr2.error_message == "Permission denied");
}

void test_remote_process_struct() {
    RemoteProcess rp;
    rp.pid = 1234;
    rp.user = "test";
    rp.command = "test";
    rp.args = "test arg";
    rp.state = ProcessState::RUNNING;
    rp.cpu_percent = 1.0;
    rp.mem_percent = 0.5;
    rp.vsz = 1000;
    rp.rss = 500;
    rp.tty = "pts/0";
    
    assert(rp.pid == 1234);
    assert(rp.state == ProcessState::RUNNING);
    assert(rp.cpu_percent == 1.0);
}

void test_local_process_info_struct() {
    LocalProcessInfo lpi;
    lpi.pid = 1234;
    lpi.name = "test.exe";
    lpi.exe_path = "C:\\test.exe";
    lpi.memory_usage = 1024 * 1024;
    
    assert(lpi.pid == 1234);
    assert(lpi.name == "test.exe");
    assert(lpi.memory_usage == 1024 * 1024);
}

int main() {
    // Initialize Winsock before running port tests
    assert(ssh_tunnel::init_winsock());
    
    test_process_state_values();
    test_kill_signal_values();
    test_parse_ps_output();
    test_parse_ps_output_running();
    test_parse_ps_output_zombie();
    test_parse_ps_output_stopped();
    test_parse_ps_output_empty();
    test_parse_ps_output_with_special_chars_in_args();
    test_kill_result_struct();
    test_remote_process_struct();
    test_local_process_info_struct();
    return 0;
}