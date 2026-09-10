#include "user_data.h"
#include <cassert>
#include <filesystem>
int main(){
 const auto root=std::filesystem::temp_directory_path()/"vant-user-data-test";std::filesystem::remove_all(root);
 {vantage::UserDataStore data(root/"data.sqlite3");data.add_bookmark({"https://example.com","Example"});data.add_bookmark({"https://example.com","Renamed"});assert(data.bookmarks().size()==1&&data.bookmarks()[0].title=="Renamed");data.set_permission("https://example.com","camera",vantage::Permission::deny);assert(data.permission("https://example.com","camera")==vantage::Permission::deny);assert(data.permission("https://example.com","geolocation")==vantage::Permission::ask);data.remove_bookmark("https://example.com");assert(data.bookmarks().empty());}
 assert(vantage::safe_download_path(root,"../../passwd")==root/"passwd");assert(vantage::safe_download_path(root,".profile")==root/"download.profile");assert(vantage::safe_download_path(root,std::string("bad\0name",8))==root/"badname");
 const auto private_path=root/"private.sqlite3";{vantage::UserDataStore data(private_path,true);data.add_bookmark({"https://private.invalid","Private"});assert(data.bookmarks().size()==1);}assert(!std::filesystem::exists(private_path));std::filesystem::remove_all(root);
}
