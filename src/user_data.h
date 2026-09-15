#ifndef VANTAGE_USER_DATA_H
#define VANTAGE_USER_DATA_H
#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>
struct sqlite3;
namespace vantage {
enum class Permission { ask, allow, deny };
struct Bookmark { std::string uri; std::string title; };
struct HistoryEntry { std::int64_t id{}; std::string uri; std::string title; std::int64_t visited_at{}; };
struct PermissionEntry { std::string origin; std::string capability; Permission decision{Permission::ask}; };
struct DownloadEntry { std::int64_t id{}; std::string uri; std::string destination; std::string status; std::int64_t created_at{}; std::uint64_t received{}; std::uint64_t total{}; };
class UserDataStore {
public:
    explicit UserDataStore(const std::filesystem::path &path, bool private_mode = false);
    ~UserDataStore();
    UserDataStore(const UserDataStore &) = delete;
    UserDataStore &operator=(const UserDataStore &) = delete;
    void add_bookmark(const Bookmark &bookmark);
    void remove_bookmark(const std::string &uri);
    void update_bookmark(const std::string &old_uri, const Bookmark &bookmark);
    std::vector<Bookmark> bookmarks() const;
    bool is_bookmarked(const std::string &uri) const;
    void set_favicon(const std::string &page_uri, const std::string &data_uri);
    std::string favicon(const std::string &page_uri) const;
    void add_history(const std::string &uri, const std::string &title, std::int64_t visited_at);
    std::vector<HistoryEntry> history(std::size_t limit = 500) const;
    void clear_history();
    void remove_history(const std::vector<std::int64_t> &ids);
    std::int64_t add_download(const std::string &uri, const std::string &destination,
                              const std::string &status, std::int64_t created_at);
    void update_download(std::int64_t id, const std::string &status);
    void update_download_progress(std::int64_t id, std::uint64_t received, std::uint64_t total);
    void reconcile_downloads();
    void remove_download(std::int64_t id);
    std::vector<DownloadEntry> downloads(std::size_t limit = 500) const;
    void set_permission(const std::string &origin, const std::string &capability, Permission permission);
    Permission permission(const std::string &origin, const std::string &capability) const;
    std::vector<PermissionEntry> permissions() const;
    bool private_mode() const noexcept { return private_mode_; }
private:
    sqlite3 *database_{};
    bool private_mode_{};
};
std::filesystem::path safe_download_path(const std::filesystem::path &directory, const std::string &suggested_name);
}
#endif
