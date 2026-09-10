#ifndef VANTAGE_USER_DATA_H
#define VANTAGE_USER_DATA_H
#include <filesystem>
#include <string>
#include <vector>
struct sqlite3;
namespace vantage {
enum class Permission { ask, allow, deny };
struct Bookmark { std::string uri; std::string title; };
class UserDataStore {
public:
    explicit UserDataStore(const std::filesystem::path &path, bool private_mode = false);
    ~UserDataStore();
    UserDataStore(const UserDataStore &) = delete;
    UserDataStore &operator=(const UserDataStore &) = delete;
    void add_bookmark(const Bookmark &bookmark);
    void remove_bookmark(const std::string &uri);
    std::vector<Bookmark> bookmarks() const;
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
