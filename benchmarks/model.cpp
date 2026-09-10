#include "browser_model.h"
#include "session_store.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <sys/resource.h>
using Clock=std::chrono::steady_clock;
int main(){
 const auto root=std::filesystem::temp_directory_path()/"vant-benchmark";std::filesystem::remove_all(root);
 std::cout<<"{\"schema_version\":1,\"workloads\":[";bool first=true;
 for(const int count:{1,10,50}){const auto start=Clock::now();vantage::BrowserModel model;for(int i=0;i<count;++i)model.new_tab("https://example.invalid/"+std::to_string(i));const auto opened=Clock::now();{vantage::SessionStore store(root/("profile-"+std::to_string(count)+".sqlite3"));store.save_tabs(model.tabs(),model.active_tab());const auto restored=store.load_tabs();if(restored.size()!=static_cast<std::size_t>(count))return 2;}const auto done=Clock::now();if(!first)std::cout<<',';first=false;std::cout<<"{\"tabs\":"<<count<<",\"model_open_us\":"<<std::chrono::duration_cast<std::chrono::microseconds>(opened-start).count()<<",\"save_restore_us\":"<<std::chrono::duration_cast<std::chrono::microseconds>(done-opened).count()<<"}";}
 rusage usage{};getrusage(RUSAGE_SELF,&usage);std::cout<<"],\"max_rss_kib\":"<<usage.ru_maxrss<<",\"scope\":\"core model and SQLite only; excludes GTK/WebKit processes and network\"}\n";std::filesystem::remove_all(root);
}
