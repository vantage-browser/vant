#include "automation.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <stdexcept>
int main(){const auto root=std::filesystem::temp_directory_path()/"vant-automation-test";std::filesystem::remove_all(root);vantage::BrowserModel model;model.new_tab();vantage::UserDataStore data(root/"data.sqlite3");
 {vantage::AutomationEngine engine(model,data,{vantage::Capability::tabs_read,vantage::Capability::tabs_write,vantage::Capability::bookmarks_write});assert(engine.evaluate_number("vant.tabCount()") == 1);assert(engine.evaluate_number("vant.openTab('example.com')") == 2);assert(model.tabs().back().uri=="https://example.com");assert(engine.evaluate_number("vant.bookmark('https://example.com','Example')")==1);assert(data.bookmarks().size()==1);}
 {vantage::AutomationEngine denied(model,data,{});try{denied.evaluate_number("vant.openTab('example.com')");assert(false);}catch(const std::runtime_error&){}try{denied.evaluate_number("vant.tabCount()") ;assert(false);}catch(const std::runtime_error&){}denied.cancel();try{denied.evaluate_number("vant.tabCount()") ;assert(false);}catch(const std::runtime_error&){} }
 {vantage::AutomationEngine limited(model,data,{vantage::Capability::tabs_read});try{limited.evaluate_number("while (true) {}");assert(false);}catch(const std::runtime_error&){}for(const char*bad:{"vant.openTab()","vant.openTab(42)","vant.bookmark('x')","vant.bookmark('file:///x','x')"}){try{limited.evaluate_number(bad);assert(false);}catch(const std::runtime_error&){}}}
 std::filesystem::remove_all(root);}
