#include "browser_model.h"

#include <cassert>

int main() {
    vantage::BrowserModel model("work");
    const auto first = model.new_tab("https://example.com");
    const auto second = model.new_tab("https://nift.dev");
    assert(model.tabs().size() == 2 && model.active_tab() == second);
    assert(model.activate(first));
    assert(model.focus() == vantage::FocusTarget::page);
    const auto duplicate = model.duplicate_tab(first);
    assert(duplicate && model.find(*duplicate)->uri == "https://example.com");
    assert(model.move_tab(*duplicate, 0));
    assert(model.tabs().front().id == *duplicate);
    assert(model.close_tab(first));
    assert(model.reopen_closed());
    assert(!model.close_tab(999999));
    assert(!model.move_tab(second, 999));
    assert(vantage::command_for_shortcut("Ctrl+T") == vantage::Command::new_tab);
    assert(vantage::command_for_shortcut("Ctrl+Shift+T") == vantage::Command::reopen_tab);
    assert(vantage::command_for_shortcut("Ctrl+N") == vantage::Command::new_window);
    assert(vantage::command_for_shortcut("Ctrl+Shift+N") == vantage::Command::new_private_window);
    assert(vantage::command_for_shortcut("Ctrl+J") == vantage::Command::downloads);
    assert(vantage::command_for_shortcut("Ctrl+H") == vantage::Command::history);
    assert(vantage::command_for_shortcut("Ctrl+D") == vantage::Command::bookmark);
    assert(!vantage::command_for_shortcut("Ctrl+Alt+Surprise"));

    vantage::BrowserModel lifecycle;
    for (int i = 0; i < 50; ++i) lifecycle.new_tab("https://example.invalid/" + std::to_string(i));
    assert(lifecycle.discard_to_limit(10) == 40);
    assert(!lifecycle.find(*lifecycle.active_tab())->discarded);
    const auto discarded_id = lifecycle.tabs().front().id;
    assert(lifecycle.find(discarded_id)->discarded);
    assert(lifecycle.activate(discarded_id));
    assert(!lifecycle.find(discarded_id)->discarded);

    vantage::BrowserModel stress;
    for (int i = 0; i < 10000; ++i) {
        const auto id = stress.new_tab();
        assert(stress.close_tab(id));
    }
    assert(stress.tabs().empty());
    for (int i = 0; i < 25; ++i) assert(stress.reopen_closed());
    assert(!stress.reopen_closed());
}
