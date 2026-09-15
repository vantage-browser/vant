#include "browser_model.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace vantage {

BrowserModel::BrowserModel(std::string workspace, std::size_t closed_limit)
    : workspace_(std::move(workspace)), closed_limit_(closed_limit) {
    if (workspace_.empty()) throw std::invalid_argument("workspace must not be empty");
}

TabId BrowserModel::new_tab(std::string uri, bool activate_tab) {
    Tab tab{next_id_++, std::move(uri), "New tab", false, false, false, false, TabLifecycle::live};
    tabs_.push_back(std::move(tab));
    if (activate_tab) active_ = tabs_.back().id;
    return tabs_.back().id;
}

Tab *BrowserModel::find(TabId id) {
    auto it = std::find_if(tabs_.begin(), tabs_.end(), [id](const Tab &tab) { return tab.id == id; });
    return it == tabs_.end() ? nullptr : &*it;
}
const Tab *BrowserModel::find(TabId id) const {
    auto it = std::find_if(tabs_.begin(), tabs_.end(), [id](const Tab &tab) { return tab.id == id; });
    return it == tabs_.end() ? nullptr : &*it;
}

bool BrowserModel::activate(TabId id) {
    auto *tab = find(id);
    if (!tab) return false;
    tab->discarded = false;
    active_ = id;
    focus_ = FocusTarget::page;
    return true;
}

std::size_t BrowserModel::discard_to_limit(std::size_t resident_limit) {
    std::size_t resident = static_cast<std::size_t>(std::count_if(tabs_.begin(), tabs_.end(), [](const Tab &tab) { return !tab.discarded; }));
    std::size_t discarded = 0;
    for (auto &tab : tabs_) {
        if (resident <= resident_limit) break;
        if ((!active_ || tab.id != *active_) && !tab.loading && !tab.discarded) {
            tab.discarded = true;
            --resident;
            ++discarded;
        }
    }
    return discarded;
}

bool BrowserModel::close_tab(TabId id) {
    auto it = std::find_if(tabs_.begin(), tabs_.end(), [id](const Tab &tab) { return tab.id == id; });
    if (it == tabs_.end()) return false;
    const auto index = static_cast<std::size_t>(it - tabs_.begin());
    if (closed_limit_ > 0) {
        auto closed = *it;
        closed.lifecycle = TabLifecycle::closed;
        closed_.push_back(std::move(closed));
        if (closed_.size() > closed_limit_) closed_.erase(closed_.begin());
    }
    tabs_.erase(it);
    if (active_ == id) {
        if (tabs_.empty()) active_.reset();
        else active_ = tabs_[std::min(index, tabs_.size() - 1)].id;
    }
    return true;
}

std::optional<TabId> BrowserModel::reopen_closed() {
    if (closed_.empty()) return std::nullopt;
    auto tab = std::move(closed_.back());
    closed_.pop_back();
    tab.id = next_id_++;
    tab.loading = false;
    tab.lifecycle = TabLifecycle::live;
    tabs_.push_back(std::move(tab));
    active_ = tabs_.back().id;
    return active_;
}

std::optional<TabId> BrowserModel::duplicate_tab(TabId id) {
    const auto *source = find(id);
    if (!source) return std::nullopt;
    const auto uri = source->uri;
    return new_tab(uri, true);
}

bool BrowserModel::move_tab(TabId id, std::size_t destination) {
    auto it = std::find_if(tabs_.begin(), tabs_.end(), [id](const Tab &tab) { return tab.id == id; });
    if (it == tabs_.end() || destination > tabs_.size()) return false;
    const auto source = static_cast<std::size_t>(std::distance(tabs_.begin(), it));
    auto tab = std::move(*it);
    tabs_.erase(it);
    if (destination > source) --destination;
    tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(destination), std::move(tab));
    return true;
}

std::optional<Command> command_for_shortcut(std::string_view shortcut) {
    if (shortcut == "Ctrl+T") return Command::new_tab;
    if (shortcut == "Ctrl+N") return Command::new_window;
    if (shortcut == "Ctrl+Shift+N") return Command::new_private_window;
    if (shortcut == "Ctrl+W") return Command::close_tab;
    if (shortcut == "Ctrl+Shift+T") return Command::reopen_tab;
    if (shortcut == "Ctrl+L") return Command::focus_address;
    if (shortcut == "Ctrl+K") return Command::command_palette;
    if (shortcut == "Ctrl+R") return Command::reload;
    if (shortcut == "Escape") return Command::stop;
    if (shortcut == "Ctrl+F") return Command::find;
    if (shortcut == "Ctrl+H") return Command::history;
    if (shortcut == "Ctrl+J") return Command::downloads;
    if (shortcut == "Ctrl+D") return Command::bookmark;
    if (shortcut == "Ctrl+Shift+Delete") return Command::delete_history;
    if (shortcut == "Ctrl+P") return Command::print;
    if (shortcut == "F11") return Command::fullscreen;
    return std::nullopt;
}

} // namespace vantage
