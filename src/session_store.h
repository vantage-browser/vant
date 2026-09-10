#ifndef VANTAGE_SESSION_STORE_H
#define VANTAGE_SESSION_STORE_H
#include "browser_model.h"
#include <filesystem>
#include <vector>
struct sqlite3;
namespace vantage {
class SessionStore {
public:
    SessionStore(const std::filesystem::path &path, bool private_mode = false);
    ~SessionStore();
    SessionStore(const SessionStore &) = delete;
    SessionStore &operator=(const SessionStore &) = delete;
    void mark_started();
    void mark_clean_shutdown();
    bool previous_exit_clean() const;
    void save_tabs(const std::vector<Tab> &tabs, std::optional<TabId> active);
    std::vector<Tab> load_tabs() const;
    bool private_mode() const noexcept { return private_mode_; }
    int schema_version() const noexcept { return 1; }
private:
    sqlite3 *database_{};
    bool private_mode_{};
    void execute(const char *sql) const;
};
}
#endif
