#include "ssh_systemd.h"
#include <cassert>
#include <string>
#include <vector>

using namespace ssh_tunnel;

void test_service_state_values() {
    assert(static_cast<int>(ServiceState::UNKNOWN) == 0);
    assert(static_cast<int>(ServiceState::ACTIVE) == 1);
    assert(static_cast<int>(ServiceState::INACTIVE) == 2);
    assert(static_cast<int>(ServiceState::ACTIVATING) == 3);
    assert(static_cast<int>(ServiceState::DEACTIVATING) == 4);
    assert(static_cast<int>(ServiceState::FAILED) == 5);
    assert(static_cast<int>(ServiceState::RELOADING) == 6);
}

void test_service_type_values() {
    assert(static_cast<int>(ServiceType::UNKNOWN) == 0);
    assert(static_cast<int>(ServiceType::SIMPLE) == 1);
    assert(static_cast<int>(ServiceType::FORKING) == 2);
    assert(static_cast<int>(ServiceType::ONESHOT) == 3);
    assert(static_cast<int>(ServiceType::DBUS) == 4);
    assert(static_cast<int>(ServiceType::NOTIFY) == 5);
    assert(static_cast<int>(ServiceType::IDLE) == 6);
}

void test_parse_systemctl_show_simple() {
    std::string output = 
        "Id=nginx.service\n"
        "Description=nginx web server\n"
        "ActiveState=active\n"
        "SubState=running\n"
        "LoadState=loaded\n"
        "Type=simple\n"
        "MainPID=1234\n"
        "ExecStart={ path=/usr/sbin/nginx argv[]=/usr/sbin/nginx -g daemon off; }\n"
        "User=www-data\n"
        "Group=www-data\n"
        "WorkingDirectory=/var/www\n"
        "Environment=PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin\n"
        "UnitFileState=enabled\n"
        "FragmentPath=/lib/systemd/system/nginx.service\n";
    
    SystemdService svc = parse_systemctl_show(output);
    
    assert(svc.name == "nginx.service");
    assert(svc.description == "nginx web server");
    assert(svc.state == ServiceState::ACTIVE);
    assert(svc.sub_state == "running");
    assert(svc.load_state == "loaded");
    assert(svc.type == ServiceType::SIMPLE);
    assert(svc.main_pid == 1234);
    assert(svc.exec_start.find("nginx") != std::string::npos);
    assert(svc.user == "www-data");
    assert(svc.group == "www-data");
    assert(svc.working_directory == "/var/www");
    assert(svc.environment.size() >= 1);
    assert(svc.enabled == true);
    assert(svc.unit_file_path == "/lib/systemd/system/nginx.service");
}

void test_parse_systemctl_show_forking() {
    std::string output = 
        "Id=redis.service\n"
        "ActiveState=active\n"
        "Type=forking\n"
        "MainPID=5678\n"
        "UnitFileState=disabled\n";
    
    SystemdService svc = parse_systemctl_show(output);
    assert(svc.type == ServiceType::FORKING);
    assert(svc.main_pid == 5678);
    assert(svc.enabled == false);
}

void test_parse_systemctl_show_oneshot() {
    std::string output = 
        "Id=cleanup.service\n"
        "Type=oneshot\n"
        "ActiveState=inactive\n";
    
    SystemdService svc = parse_systemctl_show(output);
    assert(svc.type == ServiceType::ONESHOT);
    assert(svc.state == ServiceState::INACTIVE);
}

void test_parse_systemctl_list_units() {
    std::string output = 
        "nginx.service loaded active running\n"
        "redis.service loaded active running\n"
        "cleanup.service loaded inactive dead\n"
        "failed.service loaded failed failed\n";
    
    auto units = parse_systemctl_list_units(output);
    
    assert(units.size() == 4);
    assert(units[0].first == "nginx.service");
    assert(units[0].second == ServiceState::ACTIVE);
    assert(units[1].first == "redis.service");
    assert(units[1].second == ServiceState::ACTIVE);
    assert(units[2].first == "cleanup.service");
    assert(units[2].second == ServiceState::INACTIVE);
    assert(units[3].first == "failed.service");
    assert(units[3].second == ServiceState::FAILED);
}

void test_parse_systemctl_is_enabled() {
    assert(parse_systemctl_is_enabled("enabled\n") == true);
    assert(parse_systemctl_is_enabled("enabled") == true);
    assert(parse_systemctl_is_enabled("disabled\n") == false);
    assert(parse_systemctl_is_enabled("static\n") == false);
    assert(parse_systemctl_is_enabled("masked\n") == false);
    assert(parse_systemctl_is_enabled("") == false);
}

void test_parse_service_state() {
    // Test internal helper
    assert(parse_service_state("active") == ServiceState::ACTIVE);
    assert(parse_service_state("inactive") == ServiceState::INACTIVE);
    assert(parse_service_state("activating") == ServiceState::ACTIVATING);
    assert(parse_service_state("deactivating") == ServiceState::DEACTIVATING);
    assert(parse_service_state("failed") == ServiceState::FAILED);
    assert(parse_service_state("reloading") == ServiceState::RELOADING);
    assert(parse_service_state("unknown") == ServiceState::UNKNOWN);
}

void test_parse_service_type() {
    assert(parse_service_type("simple") == ServiceType::SIMPLE);
    assert(parse_service_type("forking") == ServiceType::FORKING);
    assert(parse_service_type("oneshot") == ServiceType::ONESHOT);
    assert(parse_service_type("dbus") == ServiceType::DBUS);
    assert(parse_service_type("notify") == ServiceType::NOTIFY);
    assert(parse_service_type("idle") == ServiceType::IDLE);
    assert(parse_service_type("unknown") == ServiceType::UNKNOWN);
}

void test_systemd_service_struct() {
    SystemdService svc;
    svc.name = "test.service";
    svc.description = "Test service";
    svc.state = ServiceState::ACTIVE;
    svc.type = ServiceType::SIMPLE;
    svc.main_pid = 1234;
    svc.enabled = true;
    
    assert(svc.name == "test.service");
    assert(svc.state == ServiceState::ACTIVE);
    assert(svc.main_pid == 1234);
    assert(svc.enabled == true);
}

void test_service_operation_result_struct() {
    ServiceOperationResult res;
    res.success = true;
    res.new_state = ServiceState::ACTIVE;
    res.exit_code = 0;
    
    assert(res.success == true);
    assert(res.new_state == ServiceState::ACTIVE);
    assert(res.exit_code == 0);
}

void test_windows_service_info_struct() {
    WindowsServiceInfo info;
    info.name = "wuauserv";
    info.display_name = "Windows Update";
    info.state = "RUNNING";
    info.pid = 1234;
    
    assert(info.name == "wuauserv");
    assert(info.state == "RUNNING");
    assert(info.pid == 1234);
}

int main() {
    test_service_state_values();
    test_service_type_values();
    test_parse_systemctl_show_simple();
    test_parse_systemctl_show_forking();
    test_parse_systemctl_show_oneshot();
    test_parse_systemctl_list_units();
    test_parse_systemctl_is_enabled();
    test_parse_service_state();
    test_parse_service_type();
    test_systemd_service_struct();
    test_service_operation_result_struct();
    test_windows_service_info_struct();
    return 0;
}