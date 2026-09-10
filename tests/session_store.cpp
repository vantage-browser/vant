#include "session_store.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <stdexcept>
int main() {
    const auto root = std::filesystem::temp_directory_path() / "vant-session-store-test";
    std::filesystem::remove_all(root);
    const auto path = root / "profile.sqlite3";
    { vantage::SessionStore store(path); assert(store.previous_exit_clean()); store.mark_started(); assert(!store.previous_exit_clean()); store.save_tabs({{1,"https://example.com","Example"},{2,"vantage:new","New"}}, 2); }
    { vantage::SessionStore recovered(path); assert(!recovered.previous_exit_clean()); const auto tabs = recovered.load_tabs(); assert(tabs.size() == 2 && tabs[0].uri == "https://example.com"); recovered.mark_clean_shutdown(); }
    { vantage::SessionStore clean(path); assert(clean.previous_exit_clean()); }
    { const auto private_path = root / "must-not-exist.sqlite3"; vantage::SessionStore private_store(private_path, true); private_store.mark_started(); private_store.save_tabs({{1,"https://private.invalid","Private"}}, 1); assert(!std::filesystem::exists(private_path)); }
    const auto corrupt = root / "corrupt.sqlite3";
    { std::ofstream output(corrupt); output << "not sqlite"; }
    try { vantage::SessionStore broken(corrupt); assert(false); } catch (const std::runtime_error &) {}
    std::filesystem::remove_all(root);
}
