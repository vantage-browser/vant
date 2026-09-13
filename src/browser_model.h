#ifndef VANTAGE_BROWSER_MODEL_H
#define VANTAGE_BROWSER_MODEL_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vantage {

using TabId = std::uint64_t;
enum class FocusTarget { page, address, command_palette };

struct Tab {
    TabId id{};
    std::string uri;
    std::string title;
    bool loading{false};
    bool discarded{false};
};

class BrowserModel {
public:
    explicit BrowserModel(std::string workspace = "default", std::size_t closed_limit = 25);
    TabId new_tab(std::string uri = "vantage:new", bool activate = true);
    bool close_tab(TabId id);
    std::optional<TabId> reopen_closed();
    std::optional<TabId> duplicate_tab(TabId id);
    bool move_tab(TabId id, std::size_t destination);
    bool activate(TabId id);
    std::size_t discard_to_limit(std::size_t resident_limit);
    Tab *find(TabId id);
    const Tab *find(TabId id) const;
    const std::vector<Tab> &tabs() const noexcept { return tabs_; }
    std::optional<TabId> active_tab() const noexcept { return active_; }
    const std::string &workspace() const noexcept { return workspace_; }
    FocusTarget focus() const noexcept { return focus_; }
    void set_focus(FocusTarget focus) noexcept { focus_ = focus; }

private:
    std::string workspace_;
    std::size_t closed_limit_;
    TabId next_id_{1};
    std::vector<Tab> tabs_;
    std::vector<Tab> closed_;
    std::optional<TabId> active_;
    FocusTarget focus_{FocusTarget::page};
};

enum class Command { new_tab, new_window, new_private_window, close_tab, reopen_tab,
    focus_address, command_palette, reload, stop, find, history, downloads, bookmark,
    delete_history, print, fullscreen };
std::optional<Command> command_for_shortcut(std::string_view shortcut);

} // namespace vantage
#endif
