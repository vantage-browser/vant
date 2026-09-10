#include "session_store.h"
#include <sqlite3.h>
#include <stdexcept>
#include <string>
namespace {
void require(int result, sqlite3 *db, const char *operation) {
    if (result != SQLITE_OK && result != SQLITE_DONE && result != SQLITE_ROW)
        throw std::runtime_error(std::string(operation) + ": " + sqlite3_errmsg(db));
}
}
namespace vantage {
SessionStore::SessionStore(const std::filesystem::path &path, bool private_mode) : private_mode_(private_mode) {
    const auto target = private_mode ? std::string(":memory:") : path.string();
    if (!private_mode && target.empty()) throw std::invalid_argument("database path must not be empty");
    if (!private_mode && path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    const int result = sqlite3_open_v2(target.c_str(), &database_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (result != SQLITE_OK) {
        const std::string message = database_ ? sqlite3_errmsg(database_) : "allocation failed";
        if (database_) sqlite3_close(database_);
        database_ = nullptr;
        throw std::runtime_error("open session database: " + message);
    }
    sqlite3_busy_timeout(database_, 2000);
    execute("PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL;");
    execute("CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);"
            "CREATE TABLE IF NOT EXISTS tabs(position INTEGER PRIMARY KEY,uri TEXT NOT NULL,title TEXT NOT NULL,active INTEGER NOT NULL CHECK(active IN(0,1)));"
            "INSERT INTO meta(key,value) VALUES('schema_version','1') ON CONFLICT(key) DO NOTHING;"
            "INSERT INTO meta(key,value) VALUES('clean_shutdown','1') ON CONFLICT(key) DO NOTHING;");
}
SessionStore::~SessionStore() { if (database_) sqlite3_close(database_); }
void SessionStore::execute(const char *sql) const {
    char *error = nullptr;
    const int result = sqlite3_exec(database_, sql, nullptr, nullptr, &error);
    if (result != SQLITE_OK) {
        const std::string message = error ? error : sqlite3_errmsg(database_);
        sqlite3_free(error);
        throw std::runtime_error("session database: " + message);
    }
}
void SessionStore::mark_started() { execute("UPDATE meta SET value='0' WHERE key='clean_shutdown';"); }
void SessionStore::mark_clean_shutdown() { execute("UPDATE meta SET value='1' WHERE key='clean_shutdown';"); }
bool SessionStore::previous_exit_clean() const {
    sqlite3_stmt *raw = nullptr;
    require(sqlite3_prepare_v2(database_, "SELECT value FROM meta WHERE key='clean_shutdown'", -1, &raw, nullptr), database_, "prepare clean state");
    const int result = sqlite3_step(raw);
    const bool clean = result == SQLITE_ROW && std::string(reinterpret_cast<const char *>(sqlite3_column_text(raw, 0))) == "1";
    sqlite3_finalize(raw);
    return clean;
}
void SessionStore::save_tabs(const std::vector<Tab> &tabs, std::optional<TabId> active) {
    execute("BEGIN IMMEDIATE; DELETE FROM tabs;");
    sqlite3_stmt *raw = nullptr;
    try {
        require(sqlite3_prepare_v2(database_, "INSERT INTO tabs(position,uri,title,active) VALUES(?,?,?,?)", -1, &raw, nullptr), database_, "prepare tabs");
        for (std::size_t i = 0; i < tabs.size(); ++i) {
            sqlite3_bind_int64(raw, 1, static_cast<sqlite3_int64>(i));
            sqlite3_bind_text(raw, 2, tabs[i].uri.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(raw, 3, tabs[i].title.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(raw, 4, active && *active == tabs[i].id ? 1 : 0);
            require(sqlite3_step(raw), database_, "insert tab");
            sqlite3_reset(raw);
            sqlite3_clear_bindings(raw);
        }
        sqlite3_finalize(raw);
        raw = nullptr;
        execute("COMMIT;");
    } catch (...) {
        if (raw) sqlite3_finalize(raw);
        sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
std::vector<Tab> SessionStore::load_tabs() const {
    sqlite3_stmt *raw = nullptr;
    require(sqlite3_prepare_v2(database_, "SELECT uri,title FROM tabs ORDER BY position", -1, &raw, nullptr), database_, "prepare restore");
    std::vector<Tab> tabs;
    while (sqlite3_step(raw) == SQLITE_ROW) {
        Tab tab;
        tab.id = static_cast<TabId>(tabs.size() + 1);
        tab.uri = reinterpret_cast<const char *>(sqlite3_column_text(raw, 0));
        tab.title = reinterpret_cast<const char *>(sqlite3_column_text(raw, 1));
        tabs.push_back(std::move(tab));
    }
    sqlite3_finalize(raw);
    return tabs;
}
}
