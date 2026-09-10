#include "user_data.h"
#include <sqlite3.h>
#include <algorithm>
#include <cctype>
#include <stdexcept>
namespace {
void exec(sqlite3 *db, const char *sql) { char *error=nullptr; if (sqlite3_exec(db,sql,nullptr,nullptr,&error)!=SQLITE_OK) { std::string message=error?error:sqlite3_errmsg(db); sqlite3_free(error); throw std::runtime_error(message); } }
void require(bool ok, sqlite3 *db) { if (!ok) throw std::runtime_error(sqlite3_errmsg(db)); }
}
namespace vantage {
UserDataStore::UserDataStore(const std::filesystem::path &path, bool private_mode) : private_mode_(private_mode) {
    const auto target=private_mode?std::string(":memory:"):path.string();
    if (!private_mode && path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    if (sqlite3_open_v2(target.c_str(),&database_,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr)!=SQLITE_OK) throw std::runtime_error("open user data");
    exec(database_,"CREATE TABLE IF NOT EXISTS bookmarks(uri TEXT PRIMARY KEY,title TEXT NOT NULL);CREATE TABLE IF NOT EXISTS permissions(origin TEXT NOT NULL,capability TEXT NOT NULL,decision INTEGER NOT NULL CHECK(decision BETWEEN 0 AND 2),PRIMARY KEY(origin,capability));");
}
UserDataStore::~UserDataStore(){if(database_)sqlite3_close(database_);}
void UserDataStore::add_bookmark(const Bookmark &bookmark){
    if(bookmark.uri.empty())throw std::invalid_argument("bookmark URI must not be empty");
    sqlite3_stmt *s=nullptr; require(sqlite3_prepare_v2(database_,"INSERT INTO bookmarks(uri,title) VALUES(?,?) ON CONFLICT(uri) DO UPDATE SET title=excluded.title",-1,&s,nullptr)==SQLITE_OK,database_); sqlite3_bind_text(s,1,bookmark.uri.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(s,2,bookmark.title.c_str(),-1,SQLITE_TRANSIENT); require(sqlite3_step(s)==SQLITE_DONE,database_); sqlite3_finalize(s);
}
void UserDataStore::remove_bookmark(const std::string &uri){sqlite3_stmt*s=nullptr;require(sqlite3_prepare_v2(database_,"DELETE FROM bookmarks WHERE uri=?",-1,&s,nullptr)==SQLITE_OK,database_);sqlite3_bind_text(s,1,uri.c_str(),-1,SQLITE_TRANSIENT);require(sqlite3_step(s)==SQLITE_DONE,database_);sqlite3_finalize(s);}
std::vector<Bookmark> UserDataStore::bookmarks() const {sqlite3_stmt*s=nullptr;require(sqlite3_prepare_v2(database_,"SELECT uri,title FROM bookmarks ORDER BY title,uri",-1,&s,nullptr)==SQLITE_OK,database_);std::vector<Bookmark> out;while(sqlite3_step(s)==SQLITE_ROW)out.push_back({reinterpret_cast<const char*>(sqlite3_column_text(s,0)),reinterpret_cast<const char*>(sqlite3_column_text(s,1))});sqlite3_finalize(s);return out;}
void UserDataStore::set_permission(const std::string &origin,const std::string &capability,Permission value){if(!origin.starts_with("https://")&& !origin.starts_with("http://"))throw std::invalid_argument("permission origin must be HTTP(S)");sqlite3_stmt*s=nullptr;require(sqlite3_prepare_v2(database_,"INSERT INTO permissions(origin,capability,decision) VALUES(?,?,?) ON CONFLICT(origin,capability) DO UPDATE SET decision=excluded.decision",-1,&s,nullptr)==SQLITE_OK,database_);sqlite3_bind_text(s,1,origin.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(s,2,capability.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int(s,3,static_cast<int>(value));require(sqlite3_step(s)==SQLITE_DONE,database_);sqlite3_finalize(s);}
Permission UserDataStore::permission(const std::string &origin,const std::string &capability) const {sqlite3_stmt*s=nullptr;require(sqlite3_prepare_v2(database_,"SELECT decision FROM permissions WHERE origin=? AND capability=?",-1,&s,nullptr)==SQLITE_OK,database_);sqlite3_bind_text(s,1,origin.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(s,2,capability.c_str(),-1,SQLITE_TRANSIENT);const auto result=sqlite3_step(s);const auto value=result==SQLITE_ROW?static_cast<Permission>(sqlite3_column_int(s,0)):Permission::ask;sqlite3_finalize(s);return value;}
std::filesystem::path safe_download_path(const std::filesystem::path &directory,const std::string &suggested){
    std::string name=std::filesystem::path(suggested).filename().string();
    name.erase(std::remove_if(name.begin(),name.end(),[](unsigned char c){return c<32||c==127||c=='/'||c=='\\';}),name.end());
    if(name.empty()||name=="."||name=="..")name="download";
    if(name.front()=='.')name="download"+name;
    const auto candidate=(directory/name).lexically_normal();
    if(candidate.parent_path()!=directory.lexically_normal())throw std::runtime_error("download escaped destination");
    return candidate;
}
}
