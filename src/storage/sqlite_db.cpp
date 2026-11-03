#include "sqlite_db.h"
#include <sqlite3.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <memory>
#include <mutex>
#include <sstream>
#include <iomanip>

using namespace ssh_tunnel;

namespace {

// sqlite3_column_text() returns nullptr for SQL NULL columns; constructing a
// std::string directly from it is undefined behaviour (crash). Treat NULL as
// an empty string.
std::string column_text(sqlite3_stmt* stmt, int column) {
    const unsigned char* text = sqlite3_column_text(stmt, column);
    return text ? std::string(reinterpret_cast<const char*>(text)) : std::string();
}

} // namespace

// ------------------------------------------------------------
// Migration SQL definitions
// ------------------------------------------------------------

const char* MIGRATION_001_INITIAL_SCHEMA_UP = R"(
-- Schema version 1: Initial tables
CREATE TABLE IF NOT EXISTS schema_version (
    version INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    applied_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS migrations (
    version INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    sql_up TEXT NOT NULL,
    sql_down TEXT,
    applied_at TEXT,
    status INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS tunnel_profiles (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    ssh_host TEXT NOT NULL,
    ssh_port INTEGER NOT NULL DEFAULT 22,
    ssh_user TEXT NOT NULL,
    identity_file TEXT,
    use_agent INTEGER NOT NULL DEFAULT 1,
    local_host TEXT NOT NULL DEFAULT '127.0.0.1',
    local_port INTEGER NOT NULL DEFAULT 0,
    remote_host TEXT NOT NULL,
    remote_port INTEGER NOT NULL,
    enable_compression INTEGER NOT NULL DEFAULT 0,
    keepalive_interval_sec INTEGER NOT NULL DEFAULT 30,
    connect_timeout_sec INTEGER NOT NULL DEFAULT 15,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at TEXT NOT NULL DEFAULT (datetime('now')),
    enabled INTEGER NOT NULL DEFAULT 1,
    sort_order INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_tunnel_profiles_enabled ON tunnel_profiles(enabled);
CREATE INDEX IF NOT EXISTS idx_tunnel_profiles_sort ON tunnel_profiles(sort_order);

CREATE TABLE IF NOT EXISTS host_key_cache (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    hostname TEXT NOT NULL,
    port INTEGER NOT NULL DEFAULT 22,
    key_type TEXT NOT NULL,
    fingerprint_sha256 TEXT NOT NULL,
    public_key_blob TEXT,
    verified INTEGER NOT NULL DEFAULT 0,
    first_seen TEXT NOT NULL DEFAULT (datetime('now')),
    last_verified TEXT,
    UNIQUE(hostname, port, key_type)
);

CREATE INDEX IF NOT EXISTS idx_host_key_lookup ON host_key_cache(hostname, port);

CREATE TABLE IF NOT EXISTS tunnel_sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    profile_id INTEGER NOT NULL REFERENCES tunnel_profiles(id) ON DELETE CASCADE,
    started_at TEXT NOT NULL DEFAULT (datetime('now')),
    ended_at TEXT,
    bound_local_port INTEGER NOT NULL DEFAULT 0,
    ssh_pid INTEGER,
    end_reason TEXT,
    error_message TEXT,
    bytes_sent INTEGER NOT NULL DEFAULT 0,
    bytes_received INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_tunnel_sessions_profile ON tunnel_sessions(profile_id);
CREATE INDEX IF NOT EXISTS idx_tunnel_sessions_started ON tunnel_sessions(started_at);
)";

const char* MIGRATION_001_INITIAL_SCHEMA_DOWN = R"(
DROP TABLE IF EXISTS tunnel_sessions;
DROP TABLE IF EXISTS host_key_cache;
DROP TABLE IF EXISTS tunnel_profiles;
DROP TABLE IF EXISTS migrations;
DROP TABLE IF EXISTS schema_version;
)";

const std::vector<ssh_tunnel::Migration>& ssh_tunnel::get_builtin_migrations() {
    static const std::vector<ssh_tunnel::Migration> migrations = []() {
        std::vector<ssh_tunnel::Migration> m;
        
        ssh_tunnel::Migration m1;
        m1.version = 1;
        m1.name = "Initial schema";
        m1.sql_up = MIGRATION_001_INITIAL_SCHEMA_UP;
        m1.sql_down = MIGRATION_001_INITIAL_SCHEMA_DOWN;
        m.push_back(m1);
        
        return m;
    }();
    return migrations;
}

// ------------------------------------------------------------
// Helper functions
// ------------------------------------------------------------

static std::string now_iso8601() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &tt);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

static int64_t sqlite_get_last_insert_rowid(sqlite3* db) {
    return sqlite3_last_insert_rowid(db);
}

static bool sqlite_exec(sqlite3* db, const std::string& sql, std::string* err_out = nullptr) {
    char* err = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        if (err_out && err) *err_out = err;
        sqlite3_free(err);
        return false;
    }
    return true;
}

// ------------------------------------------------------------
// Database implementation (PIMPL)
// ------------------------------------------------------------

struct Database::Impl {
    DbConfig config;
    sqlite3* db = nullptr;
    std::string last_error;
    mutable std::mutex mtx;

    Impl(const DbConfig& cfg) : config(cfg) {}
    ~Impl() { close(); }

    void set_error(const std::string& msg) {
        last_error = msg;
    }

    bool open() {
        std::lock_guard<std::mutex> lock(mtx);
        if (db) return true;

        std::string path(config.path.begin(), config.path.end());
        int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI;
        int rc = sqlite3_open_v2(path.c_str(), &db, flags, nullptr);
        if (rc != SQLITE_OK) {
            set_error(sqlite3_errmsg(db));
            sqlite3_close(db);
            db = nullptr;
            return false;
        }

        sqlite3_busy_timeout(db, config.busy_timeout_ms);

        if (config.foreign_keys) {
            sqlite_exec(db, "PRAGMA foreign_keys = ON;");
        }

        if (config.wal_mode) {
            sqlite_exec(db, "PRAGMA journal_mode = WAL;");
        }

        sqlite_exec(db, "PRAGMA synchronous = NORMAL;");

        return true;
    }

    void close() {
        std::lock_guard<std::mutex> lock(mtx);
        if (db) {
            sqlite3_close(db);
            db = nullptr;
        }
    }

    bool exec(const std::string& sql) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!db) return false;
        return sqlite_exec(db, sql, &last_error);
    }

    template<typename Func>
    bool query(const std::string& sql, Func&& callback) {
        return query(sql, [](sqlite3_stmt*){ return true; }, std::forward<Func>(callback));
    }

    template<typename Binder, typename Func>
    bool query(const std::string& sql, Binder&& binder, Func&& callback) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!db) return false;

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            set_error(sqlite3_errmsg(db));
            return false;
        }

        if (!binder(stmt)) {
            sqlite3_finalize(stmt);
            return false;
        }

        bool ok = true;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            if (!callback(stmt)) {
                ok = false;
                break;
            }
        }

        if (rc != SQLITE_DONE && rc != SQLITE_OK) {
            set_error(sqlite3_errmsg(db));
            ok = false;
        }

        sqlite3_finalize(stmt);
        return ok;
    }

    template<typename Func>
    bool execute(const std::string& sql, Func&& binder) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!db) return false;

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            set_error(sqlite3_errmsg(db));
            return false;
        }

        if (!binder(stmt)) {
            sqlite3_finalize(stmt);
            return false;
        }

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            set_error(sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return false;
        }

        sqlite3_finalize(stmt);
        return true;
    }
};

Database::Database(const DbConfig& config) : pimpl_(std::make_unique<Impl>(config)) {}
Database::~Database() = default;

Database::Database(Database&& other) noexcept : pimpl_(std::move(other.pimpl_)) {}
Database& Database::operator=(Database&& other) noexcept {
    pimpl_ = std::move(other.pimpl_);
    return *this;
}

bool Database::initialize() {
    if (!pimpl_->open()) return false;
    return migrate();
}

uint32_t Database::get_schema_version() const {
    // Impl::query() takes the mutex itself; do not hold it here (non-recursive).
    uint32_t version = 0;
    pimpl_->query("SELECT MAX(version) FROM schema_version;",
        [&](sqlite3_stmt* stmt) {
            if (sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
                version = static_cast<uint32_t>(sqlite3_column_int(stmt, 0));
            }
            return true;
        });
    return version;
}

bool Database::migrate() {
    // Bootstrap the migration bookkeeping tables before recording migrations.
    if (!pimpl_->exec(
            "CREATE TABLE IF NOT EXISTS schema_version ("
            "version INTEGER PRIMARY KEY, name TEXT NOT NULL, "
            "applied_at TEXT NOT NULL DEFAULT (datetime('now')));")) {
        return false;
    }
    if (!pimpl_->exec(
            "CREATE TABLE IF NOT EXISTS migrations ("
            "version INTEGER PRIMARY KEY, name TEXT NOT NULL, sql_up TEXT NOT NULL, "
            "sql_down TEXT, applied_at TEXT, status INTEGER NOT NULL DEFAULT 0);")) {
        return false;
    }

    uint32_t current_version = get_schema_version();
    const auto& migrations = get_builtin_migrations();

    for (const auto& m : migrations) {
        if (m.version <= current_version) continue;

        // Use bound parameters: migration SQL contains quotes and newlines.
        const bool recorded = pimpl_->execute(
            "INSERT OR REPLACE INTO migrations (version, name, sql_up, sql_down, status) "
            "VALUES (?, ?, ?, ?, ?);",
            [&](sqlite3_stmt* stmt) {
                sqlite3_bind_int(stmt, 1, static_cast<int>(m.version));
                sqlite3_bind_text(stmt, 2, m.name.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, m.sql_up.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 4, m.sql_down.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 5, static_cast<int>(MigrationStatus::PENDING));
                return true;
            });
        if (!recorded) return false;

        if (!pimpl_->exec(m.sql_up)) {
            pimpl_->execute(
                "UPDATE migrations SET status = ? WHERE version = ?;",
                [&](sqlite3_stmt* stmt) {
                    sqlite3_bind_int(stmt, 1, static_cast<int>(MigrationStatus::FAILED));
                    sqlite3_bind_int(stmt, 2, static_cast<int>(m.version));
                    return true;
                });
            return false;
        }

        const std::string applied_at = now_iso8601();
        const bool versioned = pimpl_->execute(
            "INSERT OR REPLACE INTO schema_version (version, name, applied_at) VALUES (?, ?, ?);",
            [&](sqlite3_stmt* stmt) {
                sqlite3_bind_int(stmt, 1, static_cast<int>(m.version));
                sqlite3_bind_text(stmt, 2, m.name.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, applied_at.c_str(), -1, SQLITE_TRANSIENT);
                return true;
            });
        if (!versioned) return false;

        const bool marked = pimpl_->execute(
            "UPDATE migrations SET status = ?, applied_at = ? WHERE version = ?;",
            [&](sqlite3_stmt* stmt) {
                sqlite3_bind_int(stmt, 1, static_cast<int>(MigrationStatus::APPLIED));
                sqlite3_bind_text(stmt, 2, applied_at.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 3, static_cast<int>(m.version));
                return true;
            });
        if (!marked) return false;

        current_version = m.version;
    }
    return true;
}

std::vector<Migration> Database::get_migration_history() const {
    std::vector<Migration> result;
    pimpl_->query("SELECT version, name, sql_up, sql_down, applied_at, status FROM migrations ORDER BY version;",
        [&](sqlite3_stmt* stmt) {
            Migration m;
            m.version = sqlite3_column_int(stmt, 0);
            m.name = column_text(stmt, 1);
            m.sql_up = column_text(stmt, 2);
            m.sql_down = column_text(stmt, 3);
            m.status = static_cast<MigrationStatus>(sqlite3_column_int(stmt, 5));
            result.push_back(m);
            return true;
        });
    return result;
}

// ------------------------------------------------------------
// Tunnel profiles
// ------------------------------------------------------------

int64_t Database::create_profile(const TunnelProfile& profile) {
    int64_t id = -1;
    std::string sql = "INSERT INTO tunnel_profiles (name, ssh_host, ssh_port, ssh_user, identity_file, "
                      "use_agent, local_host, local_port, remote_host, remote_port, "
                      "enable_compression, keepalive_interval_sec, connect_timeout_sec, "
                      "enabled, sort_order) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    
    pimpl_->execute(sql, [&](sqlite3_stmt* stmt) {
        sqlite3_bind_text(stmt, 1, profile.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, profile.ssh_host.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, profile.ssh_port);
        sqlite3_bind_text(stmt, 4, profile.ssh_user.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, profile.identity_file.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 6, profile.use_agent ? 1 : 0);
        sqlite3_bind_text(stmt, 7, profile.local_host.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 8, profile.local_port);
        sqlite3_bind_text(stmt, 9, profile.remote_host.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 10, profile.remote_port);
        sqlite3_bind_int(stmt, 11, profile.enable_compression ? 1 : 0);
        sqlite3_bind_int(stmt, 12, profile.keepalive_interval_sec);
        sqlite3_bind_int(stmt, 13, profile.connect_timeout_sec);
        sqlite3_bind_int(stmt, 14, profile.enabled ? 1 : 0);
        sqlite3_bind_int(stmt, 15, profile.sort_order);
        return true;
    });

    if (pimpl_->db) {
        id = sqlite3_last_insert_rowid(pimpl_->db);
    }
    return id;
}

std::optional<TunnelProfile> Database::get_profile(int64_t id) {
    std::optional<TunnelProfile> result;
    pimpl_->query("SELECT id, name, ssh_host, ssh_port, ssh_user, identity_file, use_agent, "
                  "local_host, local_port, remote_host, remote_port, enable_compression, "
                  "keepalive_interval_sec, connect_timeout_sec, created_at, updated_at, enabled, sort_order "
                  "FROM tunnel_profiles WHERE id = ?;",
        [&](sqlite3_stmt* stmt) {
            sqlite3_bind_int64(stmt, 1, id);
            return true;
        },
        [&](sqlite3_stmt* stmt) {
            TunnelProfile p;
            p.id = sqlite3_column_int64(stmt, 0);
            p.name = column_text(stmt, 1);
            p.ssh_host = column_text(stmt, 2);
            p.ssh_port = sqlite3_column_int(stmt, 3);
            p.ssh_user = column_text(stmt, 4);
            p.identity_file = column_text(stmt, 5);
            p.use_agent = sqlite3_column_int(stmt, 6);
            p.local_host = column_text(stmt, 7);
            p.local_port = sqlite3_column_int(stmt, 8);
            p.remote_host = column_text(stmt, 9);
            p.remote_port = sqlite3_column_int(stmt, 10);
            p.enable_compression = sqlite3_column_int(stmt, 11);
            p.keepalive_interval_sec = sqlite3_column_int(stmt, 12);
            p.connect_timeout_sec = sqlite3_column_int(stmt, 13);
            p.created_at = std::chrono::system_clock::now();
            p.updated_at = std::chrono::system_clock::now();
            p.enabled = sqlite3_column_int(stmt, 16);
            p.sort_order = sqlite3_column_int(stmt, 17);
            result = p;
            return false;
        });
    return result;
}

std::vector<TunnelProfile> Database::get_all_profiles(bool enabled_only) {
    std::vector<TunnelProfile> result;
    std::string sql = "SELECT id, name, ssh_host, ssh_port, ssh_user, identity_file, use_agent, "
                      "local_host, local_port, remote_host, remote_port, enable_compression, "
                      "keepalive_interval_sec, connect_timeout_sec, created_at, updated_at, enabled, sort_order "
                      "FROM tunnel_profiles ";
    if (enabled_only) sql += "WHERE enabled = 1 ";
    sql += "ORDER BY sort_order, name;";

    pimpl_->query(sql, [&](sqlite3_stmt* stmt) {
        TunnelProfile p;
        p.id = sqlite3_column_int64(stmt, 0);
        p.name = column_text(stmt, 1);
        p.ssh_host = column_text(stmt, 2);
        p.ssh_port = sqlite3_column_int(stmt, 3);
        p.ssh_user = column_text(stmt, 4);
        p.identity_file = column_text(stmt, 5);
        p.use_agent = sqlite3_column_int(stmt, 6);
        p.local_host = column_text(stmt, 7);
        p.local_port = sqlite3_column_int(stmt, 8);
        p.remote_host = column_text(stmt, 9);
        p.remote_port = sqlite3_column_int(stmt, 10);
        p.enable_compression = sqlite3_column_int(stmt, 11);
        p.keepalive_interval_sec = sqlite3_column_int(stmt, 12);
        p.connect_timeout_sec = sqlite3_column_int(stmt, 13);
        p.created_at = std::chrono::system_clock::now();
        p.updated_at = std::chrono::system_clock::now();
        p.enabled = sqlite3_column_int(stmt, 16);
        p.sort_order = sqlite3_column_int(stmt, 17);
        result.push_back(p);
        return true;
    });
    return result;
}

bool Database::update_profile(const TunnelProfile& profile) {
    std::string sql = "UPDATE tunnel_profiles SET "
                      "name = ?, ssh_host = ?, ssh_port = ?, ssh_user = ?, identity_file = ?, "
                      "use_agent = ?, local_host = ?, local_port = ?, remote_host = ?, remote_port = ?, "
                      "enable_compression = ?, keepalive_interval_sec = ?, connect_timeout_sec = ?, "
                      "enabled = ?, sort_order = ?, updated_at = datetime('now') "
                      "WHERE id = ?;";
    return pimpl_->execute(sql, [&](sqlite3_stmt* stmt) {
        sqlite3_bind_text(stmt, 1, profile.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, profile.ssh_host.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, profile.ssh_port);
        sqlite3_bind_text(stmt, 4, profile.ssh_user.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, profile.identity_file.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 6, profile.use_agent ? 1 : 0);
        sqlite3_bind_text(stmt, 7, profile.local_host.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 8, profile.local_port);
        sqlite3_bind_text(stmt, 9, profile.remote_host.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 10, profile.remote_port);
        sqlite3_bind_int(stmt, 11, profile.enable_compression ? 1 : 0);
        sqlite3_bind_int(stmt, 12, profile.keepalive_interval_sec);
        sqlite3_bind_int(stmt, 13, profile.connect_timeout_sec);
        sqlite3_bind_int(stmt, 14, profile.enabled ? 1 : 0);
        sqlite3_bind_int(stmt, 15, profile.sort_order);
        sqlite3_bind_int64(stmt, 16, profile.id);
        return true;
    });
}

bool Database::delete_profile(int64_t id) {
    return pimpl_->exec("DELETE FROM tunnel_profiles WHERE id = " + std::to_string(id) + ";");
}

bool Database::set_profile_enabled(int64_t id, bool enabled) {
    return pimpl_->exec("UPDATE tunnel_profiles SET enabled = " + std::to_string(enabled ? 1 : 0) +
                        ", updated_at = datetime('now') WHERE id = " + std::to_string(id) + ";");
}

bool Database::reorder_profiles(const std::vector<int64_t>& ordered_ids) {
    for (size_t i = 0; i < ordered_ids.size(); ++i) {
        std::string sql = "UPDATE tunnel_profiles SET sort_order = " + std::to_string(i) +
                          " WHERE id = " + std::to_string(ordered_ids[i]) + ";";
        if (!pimpl_->exec(sql)) return false;
    }
    return true;
}

// ------------------------------------------------------------
// Host key cache
// ------------------------------------------------------------

int64_t Database::upsert_host_key(const HostKeyCache& key) {
    std::string sql = "INSERT INTO host_key_cache (hostname, port, key_type, fingerprint_sha256, "
                      "public_key_blob, verified, first_seen, last_verified) "
                      "VALUES (?, ?, ?, ?, ?, ?, datetime('now'), ?) "
                      "ON CONFLICT(hostname, port, key_type) DO UPDATE SET "
                      "fingerprint_sha256 = excluded.fingerprint_sha256, "
                      "public_key_blob = excluded.public_key_blob, "
                      "verified = excluded.verified, "
                      "last_verified = excluded.last_verified;";
    
    pimpl_->execute(sql, [&](sqlite3_stmt* stmt) {
        sqlite3_bind_text(stmt, 1, key.hostname.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, key.port);
        sqlite3_bind_text(stmt, 3, key.key_type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, key.fingerprint_sha256.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, key.public_key_blob.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 6, key.verified ? 1 : 0);
        sqlite3_bind_text(stmt, 7, now_iso8601().c_str(), -1, SQLITE_TRANSIENT);
        return true;
    });

    if (pimpl_->db) {
        return sqlite3_last_insert_rowid(pimpl_->db);
    }
    return -1;
}

std::optional<HostKeyCache> Database::get_host_key(std::string_view hostname, uint16_t port) {
    std::optional<HostKeyCache> result;
    pimpl_->query("SELECT id, hostname, port, key_type, fingerprint_sha256, public_key_blob, "
                  "verified, first_seen, last_verified "
                  "FROM host_key_cache WHERE hostname = ? AND port = ?;",
        [&](sqlite3_stmt* stmt) {
            sqlite3_bind_text(stmt, 1, hostname.data(), hostname.size(), SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, port);
            return true;
        },
        [&](sqlite3_stmt* stmt) {
            HostKeyCache k;
            k.id = sqlite3_column_int64(stmt, 0);
            k.hostname = column_text(stmt, 1);
            k.port = sqlite3_column_int(stmt, 2);
            k.key_type = column_text(stmt, 3);
            k.fingerprint_sha256 = column_text(stmt, 4);
            k.public_key_blob = column_text(stmt, 5);
            k.verified = sqlite3_column_int(stmt, 6);
            k.first_seen = std::chrono::system_clock::now();
            k.last_verified = std::chrono::system_clock::now();
            result = k;
            return false;
        });
    return result;
}

std::vector<HostKeyCache> Database::get_all_host_keys() {
    std::vector<HostKeyCache> result;
    pimpl_->query("SELECT id, hostname, port, key_type, fingerprint_sha256, public_key_blob, "
                  "verified, first_seen, last_verified FROM host_key_cache;",
        [&](sqlite3_stmt* stmt) {
            HostKeyCache k;
            k.id = sqlite3_column_int64(stmt, 0);
            k.hostname = column_text(stmt, 1);
            k.port = sqlite3_column_int(stmt, 2);
            k.key_type = column_text(stmt, 3);
            k.fingerprint_sha256 = column_text(stmt, 4);
            k.public_key_blob = column_text(stmt, 5);
            k.verified = sqlite3_column_int(stmt, 6);
            k.first_seen = std::chrono::system_clock::now();
            k.last_verified = std::chrono::system_clock::now();
            result.push_back(k);
            return true;
        });
    return result;
}

bool Database::delete_host_key(std::string_view hostname, uint16_t port) {
    std::string sql = "DELETE FROM host_key_cache WHERE hostname = ? AND port = ?;";
    return pimpl_->execute(sql, [&](sqlite3_stmt* stmt) {
        sqlite3_bind_text(stmt, 1, hostname.data(), hostname.size(), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, port);
        return true;
    });
}

bool Database::mark_host_key_verified(std::string_view hostname, uint16_t port, bool verified) {
    std::string sql = "UPDATE host_key_cache SET verified = ?, last_verified = datetime('now') "
                      "WHERE hostname = ? AND port = ?;";
    return pimpl_->execute(sql, [&](sqlite3_stmt* stmt) {
        sqlite3_bind_int(stmt, 1, verified ? 1 : 0);
        sqlite3_bind_text(stmt, 2, hostname.data(), hostname.size(), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, port);
        return true;
    });
}

// ------------------------------------------------------------
// Tunnel sessions
// ------------------------------------------------------------

int64_t Database::create_session(const TunnelSession& session) {
    std::string sql = "INSERT INTO tunnel_sessions (profile_id, started_at, bound_local_port, ssh_pid) "
                      "VALUES (?, datetime('now'), ?, ?);";
    
    pimpl_->execute(sql, [&](sqlite3_stmt* stmt) {
        sqlite3_bind_int64(stmt, 1, session.profile_id);
        sqlite3_bind_int(stmt, 2, session.bound_local_port);
        sqlite3_bind_int(stmt, 3, session.ssh_pid);
        return true;
    });

    if (pimpl_->db) {
        return sqlite3_last_insert_rowid(pimpl_->db);
    }
    return -1;
}

std::optional<TunnelSession> Database::get_session(int64_t id) {
    std::optional<TunnelSession> result;
    pimpl_->query("SELECT id, profile_id, started_at, ended_at, bound_local_port, ssh_pid, "
                  "end_reason, error_message, bytes_sent, bytes_received "
                  "FROM tunnel_sessions WHERE id = ?;",
        [&](sqlite3_stmt* stmt) {
            sqlite3_bind_int64(stmt, 1, id);
            return true;
        },
        [&](sqlite3_stmt* stmt) {
            TunnelSession s;
            s.id = sqlite3_column_int64(stmt, 0);
            s.profile_id = sqlite3_column_int64(stmt, 1);
            s.started_at = std::chrono::system_clock::now();
            s.ended_at = std::chrono::system_clock::now();
            s.bound_local_port = sqlite3_column_int(stmt, 4);
            s.ssh_pid = sqlite3_column_int(stmt, 5);
            s.end_reason = column_text(stmt, 6);
            s.error_message = column_text(stmt, 7);
            s.bytes_sent = sqlite3_column_int64(stmt, 8);
            s.bytes_received = sqlite3_column_int64(stmt, 9);
            result = s;
            return false;
        });
    return result;
}

std::vector<TunnelSession> Database::get_sessions_for_profile(int64_t profile_id, size_t limit) {
    std::vector<TunnelSession> result;
    // Simplified implementation
    return result;
}

bool Database::update_session(const TunnelSession& session) {
    std::string sql = "UPDATE tunnel_sessions SET "
                      "ended_at = ?, bound_local_port = ?, ssh_pid = ?, "
                      "end_reason = ?, error_message = ?, bytes_sent = ?, bytes_received = ? "
                      "WHERE id = ?;";
    return pimpl_->execute(sql, [&](sqlite3_stmt* stmt) {
        sqlite3_bind_text(stmt, 1, now_iso8601().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, session.bound_local_port);
        sqlite3_bind_int(stmt, 3, session.ssh_pid);
        sqlite3_bind_text(stmt, 4, session.end_reason.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, session.error_message.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 6, session.bytes_sent);
        sqlite3_bind_int64(stmt, 7, session.bytes_received);
        sqlite3_bind_int64(stmt, 8, session.id);
        return true;
    });
}

bool Database::end_session(int64_t id, std::string_view end_reason, std::string_view error_msg) {
    std::string sql = "UPDATE tunnel_sessions SET "
                      "ended_at = datetime('now'), end_reason = ?, error_message = ? "
                      "WHERE id = ?;";
    return pimpl_->execute(sql, [&](sqlite3_stmt* stmt) {
        sqlite3_bind_text(stmt, 1, end_reason.data(), end_reason.size(), SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, error_msg.data(), error_msg.size(), SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, id);
        return true;
    });
}

// ------------------------------------------------------------
// Utility
// ------------------------------------------------------------

bool Database::vacuum() {
    return pimpl_->exec("VACUUM;");
}

bool Database::backup(std::wstring_view backup_path) {
    std::lock_guard<std::mutex> lock(pimpl_->mtx);
    if (!pimpl_->db) return false;

    sqlite3* backup_db = nullptr;
    std::string path(backup_path.begin(), backup_path.end());
    int rc = sqlite3_open_v2(path.c_str(), &backup_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        if (backup_db) sqlite3_close(backup_db);
        return false;
    }

    sqlite3_backup* backup = sqlite3_backup_init(backup_db, "main", pimpl_->db, "main");
    if (!backup) {
        sqlite3_close(backup_db);
        return false;
    }

    rc = sqlite3_backup_step(backup, -1);
    sqlite3_backup_finish(backup);
    sqlite3_close(backup_db);
    return rc == SQLITE_DONE;
}

std::string Database::get_last_error() const {
    return pimpl_->last_error;
}

bool Database::is_open() const noexcept {
    return pimpl_->db != nullptr;
}
