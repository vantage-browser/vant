#ifndef VANTAGE_AUTOMATION_H
#define VANTAGE_AUTOMATION_H
#include "browser_model.h"
#include "navigation.h"
#include "user_data.h"
#include <set>
#include <string>
struct js_runtime;
namespace vantage {
enum class Capability { tabs_read, tabs_write, bookmarks_write, network, filesystem, clipboard, page };
class AutomationEngine {
public:
    AutomationEngine(BrowserModel &model, UserDataStore &data, std::set<Capability> grants = {});
    ~AutomationEngine();AutomationEngine(const AutomationEngine&)=delete;AutomationEngine&operator=(const AutomationEngine&)=delete;
    double evaluate_number(const std::string &source);
    void cancel() noexcept { cancelled_=true; }
    void reset_cancel() noexcept { cancelled_=false; }
    bool granted(Capability capability) const { return grants_.contains(capability); }
    BrowserModel &model() noexcept { return model_; }
    UserDataStore &data() noexcept { return data_; }
    NavigationPolicy &navigation() noexcept { return navigation_; }
    bool cancelled() const noexcept { return cancelled_; }
private:
    BrowserModel &model_;UserDataStore &data_;NavigationPolicy navigation_;std::set<Capability> grants_;js_runtime *runtime_{};bool cancelled_{};
};
}
#endif
