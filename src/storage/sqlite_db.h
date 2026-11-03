#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <chrono>
#include <memory>

namespace ssh_tunnel {

// ------------------------------------------------------------
// SQLite database schema and migrations
// ------------------------------------------------------------

enum class MigrationStatus : uint8_t {
    PENDING = 0,
    APPLIED = 1,
    FAILED  = 2,
};

struct Migration {
    uint32_t version = 0;       // e.g. 1, 2, 3...
    std::string name;           // descriptive name
    std::string sql_up;         // SQL to apply
    std::string sql_down;       // SQL to rollback (optional)
    std::chrono::system_clock::time_point applied_at;
    MigrationStatus status = MigrationStatus::PENDING;
};

struct DbConfig {
    std::wstring path;          // Database file path
    bool create_if_missing = true;
    int busy_timeout_ms = 5000;
    bool foreign_keys = true;
    bool wal_mode = true;
};

// Tunnel profile stored in DB
struct TunnelProfile {
    int64_t id = 0;
    std::string name;           // User-friendly name
    std::string ssh_host;
    uint16_t ssh_port = 22;
    std::string ssh_user;
    std::string identity_file;  // Path to private key (not the key itself)
    bool use_agent = true;
    std::string local_host;     // Local bind address
    uint16_t local_port = 0;    // 0 = auto
    std::string remote_host;
    uint16_t remote_port = 0;
    bool enable_compression = false;
    uint32_t keepalive_interval_sec = 30;
    uint32_t connect_timeout_sec = 15;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
    bool enabled = true;
    int sort_order = 0;
};

// SSH host key cache (known_hosts mirror in DB for quick lookup)
struct HostKeyCache {
    int64_t id = 0;
    std::string hostname;
    uint16_t port = 22;
    std::string key_type;       // rsa, ecdsa, ed25519
    std::string fingerprint_sha256;
    std::string public_key_blob; // base64
    bool verified = false;
    std::chrono::system_clock::time_point first_seen;
    std::chrono::system_clock::time_point last_verified;
};

// Tunnel session history
struct TunnelSession {
    int64_t id = 0;
    int64_t profile_id = 0;
    std::chrono::system_clock::time_point started_at;
    std::chrono::system_clock::time_point ended_at;
    uint16_t bound_local_port = 0;
    uint32_t ssh_pid = 0;
    std::string end_reason;     // stopped, error, crash, timeout
    std::string error_message;
    uint64_t bytes_sent = 0;
    uint64_t bytes_received = 0;
};

// ------------------------------------------------------------
// Database interface
// ------------------------------------------------------------

class Database {
public:
    explicit Database(const DbConfig& config);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) noexcept;
    Database& operator=(Database&&) noexcept;

    // Initialize schema (run migrations)
    bool initialize();

    // Get current schema version
    uint32_t get_schema_version() const;

    // Run pending migrations
    bool migrate();

    // Get migration history
    std::vector<Migration> get_migration_history() const;

    // --------------------------------------------------------
    // Tunnel profiles
    // --------------------------------------------------------
    int64_t create_profile(const TunnelProfile& profile);
    std::optional<TunnelProfile> get_profile(int64_t id);
    std::vector<TunnelProfile> get_all_profiles(bool enabled_only = false);
    bool update_profile(const TunnelProfile& profile);
    bool delete_profile(int64_t id);
    bool set_profile_enabled(int64_t id, bool enabled);
    bool reorder_profiles(const std::vector<int64_t>& ordered_ids);

    // --------------------------------------------------------
    // Host key cache
    // --------------------------------------------------------
    int64_t upsert_host_key(const HostKeyCache& key);
    std::optional<HostKeyCache> get_host_key(std::string_view hostname, uint16_t port);
    std::vector<HostKeyCache> get_all_host_keys();
    bool delete_host_key(std::string_view hostname, uint16_t port);
    bool mark_host_key_verified(std::string_view hostname, uint16_t port, bool verified);

    // --------------------------------------------------------
    // Tunnel sessions
    // --------------------------------------------------------
    int64_t create_session(const TunnelSession& session);
    std::optional<TunnelSession> get_session(int64_t id);
    std::vector<TunnelSession> get_sessions_for_profile(int64_t profile_id, size_t limit = 100);
    bool update_session(const TunnelSession& session);
    bool end_session(int64_t id, std::string_view end_reason, std::string_view error_msg = "");

    // --------------------------------------------------------
    // Utility
    // --------------------------------------------------------
    bool vacuum();
    bool backup(std::wstring_view backup_path);
    std::string get_last_error() const;
    bool is_open() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

// ------------------------------------------------------------
// Built-in migrations (version 1 = initial schema)
// ------------------------------------------------------------

// Returns the list of built-in migrations in order
const std::vector<Migration>& get_builtin_migrations();

} // namespace ssh_tunnel