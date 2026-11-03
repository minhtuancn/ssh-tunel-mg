#include "sqlite_db.h"
#include <cassert>
#include <string>
#include <vector>
#include <chrono>

using namespace ssh_tunnel;

void test_db_config_defaults() {
    DbConfig cfg;
    assert(cfg.path.empty());
    assert(cfg.create_if_missing == true);
    assert(cfg.busy_timeout_ms == 5000);
    assert(cfg.foreign_keys == true);
    assert(cfg.wal_mode == true);
}

void test_tunnel_profile_defaults() {
    TunnelProfile p;
    assert(p.id == 0);
    assert(p.name.empty());
    assert(p.ssh_host.empty());
    assert(p.ssh_port == 22);
    assert(p.ssh_user.empty());
    assert(p.identity_file.empty());
    assert(p.use_agent == true);
    assert(p.local_host.empty());
    assert(p.local_port == 0);
    assert(p.remote_host.empty());
    assert(p.remote_port == 0);
    assert(p.enable_compression == false);
    assert(p.keepalive_interval_sec == 30);
    assert(p.connect_timeout_sec == 15);
    assert(p.enabled == true);
    assert(p.sort_order == 0);
}

void test_host_key_cache_defaults() {
    HostKeyCache k;
    assert(k.id == 0);
    assert(k.hostname.empty());
    assert(k.port == 22);
    assert(k.key_type.empty());
    assert(k.fingerprint_sha256.empty());
    assert(k.public_key_blob.empty());
    assert(k.verified == false);
}

void test_tunnel_session_defaults() {
    TunnelSession s;
    assert(s.id == 0);
    assert(s.profile_id == 0);
    assert(s.bound_local_port == 0);
    assert(s.ssh_pid == 0);
    assert(s.end_reason.empty());
    assert(s.error_message.empty());
    assert(s.bytes_sent == 0);
    assert(s.bytes_received == 0);
}

void test_migration_defaults() {
    Migration m;
    assert(m.version == 0);
    assert(m.name.empty());
    assert(m.sql_up.empty());
    assert(m.sql_down.empty());
    assert(m.status == MigrationStatus::PENDING);
}

void test_migration_status_values() {
    assert(static_cast<int>(MigrationStatus::PENDING) == 0);
    assert(static_cast<int>(MigrationStatus::APPLIED) == 1);
    assert(static_cast<int>(MigrationStatus::FAILED) == 2);
}

void test_database_create_and_open() {
    DbConfig cfg;
    cfg.path = L":memory:";  // In-memory database
    cfg.create_if_missing = true;
    
    Database db(cfg);
    assert(db.initialize() == true);
    assert(db.is_open() == true);
    assert(db.get_schema_version() == 1);
}

void test_database_create_profile() {
    DbConfig cfg;
    cfg.path = L":memory:";
    Database db(cfg);
    assert(db.initialize());
    
    TunnelProfile profile;
    profile.name = "Test Profile";
    profile.ssh_host = "example.com";
    profile.ssh_port = 22;
    profile.ssh_user = "testuser";
    profile.remote_host = "127.0.0.1";
    profile.remote_port = 80;
    
    int64_t id = db.create_profile(profile);
    assert(id > 0);
    
    auto retrieved = db.get_profile(id);
    assert(retrieved.has_value());
    assert(retrieved->name == "Test Profile");
    assert(retrieved->ssh_host == "example.com");
    assert(retrieved->ssh_port == 22);
    assert(retrieved->ssh_user == "testuser");
    assert(retrieved->remote_host == "127.0.0.1");
    assert(retrieved->remote_port == 80);
    assert(retrieved->enabled == true);
}

void test_database_get_all_profiles() {
    DbConfig cfg;
    cfg.path = L":memory:";
    Database db(cfg);
    assert(db.initialize());
    
    // Create multiple profiles
    for (int i = 0; i < 3; ++i) {
        TunnelProfile p;
        p.name = "Profile " + std::to_string(i);
        p.ssh_host = "host" + std::to_string(i) + ".com";
        p.ssh_user = "user";
        p.remote_host = "127.0.0.1";
        p.remote_port = 80;
        db.create_profile(p);
    }
    
    auto profiles = db.get_all_profiles();
    assert(profiles.size() == 3);
    
    auto enabled_only = db.get_all_profiles(true);
    assert(enabled_only.size() == 3);
}

void test_database_update_profile() {
    DbConfig cfg;
    cfg.path = L":memory:";
    Database db(cfg);
    assert(db.initialize());
    
    TunnelProfile p;
    p.name = "Original";
    p.ssh_host = "host.com";
    p.ssh_user = "user";
    p.remote_host = "127.0.0.1";
    p.remote_port = 80;
    int64_t id = db.create_profile(p);
    
    auto retrieved = db.get_profile(id);
    assert(retrieved.has_value());
    assert(retrieved->name == "Original");
    
    retrieved->name = "Updated";
    retrieved->ssh_port = 2222;
    assert(db.update_profile(*retrieved));
    
    retrieved = db.get_profile(id);
    assert(retrieved.has_value());
    assert(retrieved->name == "Updated");
    assert(retrieved->ssh_port == 2222);
}

void test_database_delete_profile() {
    DbConfig cfg;
    cfg.path = L":memory:";
    Database db(cfg);
    assert(db.initialize());
    
    TunnelProfile p;
    p.name = "ToDelete";
    p.ssh_host = "host.com";
    p.ssh_user = "user";
    p.remote_host = "127.0.0.1";
    p.remote_port = 80;
    int64_t id = db.create_profile(p);
    
    assert(db.delete_profile(id));
    
    auto retrieved = db.get_profile(id);
    assert(!retrieved.has_value());
}

void test_database_reorder_profiles() {
    DbConfig cfg;
    cfg.path = L":memory:";
    Database db(cfg);
    assert(db.initialize());
    
    std::vector<int64_t> ids;
    for (int i = 0; i < 3; ++i) {
        TunnelProfile p;
        p.name = "Profile " + std::to_string(i);
        p.ssh_host = "host.com";
        p.ssh_user = "user";
        p.remote_host = "127.0.0.1";
        p.remote_port = 80;
        ids.push_back(db.create_profile(p));
    }
    
    // Reverse order
    std::vector<int64_t> reversed = {ids[2], ids[1], ids[0]};
    assert(db.reorder_profiles(reversed));
    
    auto profiles = db.get_all_profiles();
    assert(profiles[0].name == "Profile 2");
    assert(profiles[1].name == "Profile 1");
    assert(profiles[2].name == "Profile 0");
}

void test_database_host_key_cache() {
    DbConfig cfg;
    cfg.path = L":memory:";
    Database db(cfg);
    assert(db.initialize());
    
    HostKeyCache key;
    key.hostname = "example.com";
    key.port = 22;
    key.key_type = "ssh-rsa";
    key.fingerprint_sha256 = "SHA256:abc123";
    key.public_key_blob = "AAAAB3NzaC1yc2E...";
    key.verified = true;
    
    int64_t id = db.upsert_host_key(key);
    assert(id > 0);
    
    auto retrieved = db.get_host_key("example.com", 22);
    assert(retrieved.has_value());
    assert(retrieved->hostname == "example.com");
    assert(retrieved->port == 22);
    assert(retrieved->key_type == "ssh-rsa");
    assert(retrieved->fingerprint_sha256 == "SHA256:abc123");
    assert(retrieved->verified == true);
}

void test_database_host_key_update() {
    DbConfig cfg;
    cfg.path = L":memory:";
    Database db(cfg);
    assert(db.initialize());
    
    HostKeyCache key;
    key.hostname = "example.com";
    key.port = 22;
    key.key_type = "ssh-rsa";
    key.fingerprint_sha256 = "SHA256:old";
    key.verified = true;
    db.upsert_host_key(key);
    
    // Update with new fingerprint
    key.fingerprint_sha256 = "SHA256:new";
    key.verified = true;
    db.upsert_host_key(key);
    
    auto retrieved = db.get_host_key("example.com", 22);
    assert(retrieved.has_value());
    assert(retrieved->fingerprint_sha256 == "SHA256:new");
}

void test_database_mark_host_key_verified() {
    DbConfig cfg;
    cfg.path = L":memory:";
    Database db(cfg);
    assert(db.initialize());
    
    HostKeyCache key;
    key.hostname = "example.com";
    key.port = 22;
    key.key_type = "ssh-rsa";
    key.fingerprint_sha256 = "SHA256:test";
    key.verified = false;
    db.upsert_host_key(key);
    
    auto retrieved = db.get_host_key("example.com", 22);
    assert(retrieved.has_value());
    assert(retrieved->verified == false);
    
    assert(db.mark_host_key_verified("example.com", 22, true));
    
    retrieved = db.get_host_key("example.com", 22);
    assert(retrieved.has_value());
    assert(retrieved->verified == true);
}

void test_database_tunnel_session() {
    DbConfig cfg;
    cfg.path = L":memory:";
    Database db(cfg);
    assert(db.initialize());
    
    // Create a profile first
    TunnelProfile p;
    p.name = "Test";
    p.ssh_host = "host.com";
    p.ssh_user = "user";
    p.remote_host = "127.0.0.1";
    p.remote_port = 80;
    int64_t profile_id = db.create_profile(p);
    
    TunnelSession session;
    session.profile_id = profile_id;
    session.bound_local_port = 8080;
    session.ssh_pid = 1234;
    
    int64_t session_id = db.create_session(session);
    assert(session_id > 0);
    
    auto retrieved = db.get_session(session_id);
    assert(retrieved.has_value());
    assert(retrieved->profile_id == profile_id);
    assert(retrieved->bound_local_port == 8080);
    assert(retrieved->ssh_pid == 1234);
}

void test_database_end_session() {
    DbConfig cfg;
    cfg.path = L":memory:";
    Database db(cfg);
    assert(db.initialize());
    
    TunnelProfile p;
    p.name = "Test";
    p.ssh_host = "host.com";
    p.ssh_user = "user";
    p.remote_host = "127.0.0.1";
    p.remote_port = 80;
    int64_t profile_id = db.create_profile(p);
    
    TunnelSession session;
    session.profile_id = profile_id;
    int64_t session_id = db.create_session(session);
    
    assert(db.end_session(session_id, "stopped", ""));
    
    auto retrieved = db.get_session(session_id);
    assert(retrieved.has_value());
    assert(retrieved->end_reason == "stopped");
}

void test_database_vacuum() {
    DbConfig cfg;
    cfg.path = L":memory:";
    Database db(cfg);
    assert(db.initialize());
    
    // Create and delete some data
    for (int i = 0; i < 10; ++i) {
        TunnelProfile p;
        p.name = "Temp " + std::to_string(i);
        p.ssh_host = "host.com";
        p.ssh_user = "user";
        p.remote_host = "127.0.0.1";
        p.remote_port = 80;
        db.create_profile(p);
    }
    
    // Delete all
    auto profiles = db.get_all_profiles();
    for (const auto& p : profiles) {
        db.delete_profile(p.id);
    }
    
    // Vacuum should succeed
    assert(db.vacuum());
}

void test_migration_history() {
    DbConfig cfg;
    cfg.path = L":memory:";
    Database db(cfg);
    assert(db.initialize());
    
    auto history = db.get_migration_history();
    assert(history.size() >= 1);
    assert(history[0].version == 1);
    assert(history[0].name == "Initial schema");
    assert(history[0].status == MigrationStatus::APPLIED);
}

void test_get_builtin_migrations() {
    const auto& migrations = get_builtin_migrations();
    assert(migrations.size() >= 1);
    assert(migrations[0].version == 1);
    assert(migrations[0].name == "Initial schema");
    assert(!migrations[0].sql_up.empty());
    assert(!migrations[0].sql_down.empty());
}

int main() {
    test_db_config_defaults();
    test_tunnel_profile_defaults();
    test_host_key_cache_defaults();
    test_tunnel_session_defaults();
    test_migration_defaults();
    test_migration_status_values();
    test_database_create_and_open();
    test_database_create_profile();
    test_database_get_all_profiles();
    test_database_update_profile();
    test_database_delete_profile();
    test_database_reorder_profiles();
    test_database_host_key_cache();
    test_database_host_key_update();
    test_database_mark_host_key_verified();
    test_database_tunnel_session();
    test_database_end_session();
    test_database_vacuum();
    test_migration_history();
    test_get_builtin_migrations();
    return 0;
}