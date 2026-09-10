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
struct DownloadEntry { std::int64_t id{}; std::string uri; std::string destination; std::string status; std::int64_t created_at{}; };
class UserDataStore {
public:
    explicit UserDataStore(const std::filesystem::path &path, bool private_mode = false);
    ~UserDataStore();
    UserDataStore(const UserDataStore &) = delete;
    UserDataStore &operator=(const UserDataStore &) = delete;
    void add_bookmark(const Bookmark &bookmark);
    void remove_bookmark(const std::string &uri);
    std::vector<Bookmark> bookmarks() const;
    bool is_bookmarked(const std::string &uri) const;
    void add_history(const std::string &uri, const std::string &title, std::int64_t visited_at);
    std::vector<HistoryEntry> history(std::size_t limit = 500) const;
    void clear_history();
    std::int64_t add_download(const std::string &uri, const std::string &destination,
                              const std::string &status, std::int64_t created_at);
    void update_download(std::int64_t id, const std::string &status);
    std::vector<DownloadEntry> downloads(std::size_t limit = 500) const;
    void set_permission(const std::string &origin, const std::string &capability, Permission permission);
    Permission permission(const std::string &origin, const std::string &capability) const;
    bool private_mode() const noexcept { return private_mode_; }
private:
    sqlite3 *database_{};
    bool private_mode_{};
};
std::filesystem::path safe_download_path(const std::filesystem::path &directory, const std::string &suggested_name);
}
#endif
