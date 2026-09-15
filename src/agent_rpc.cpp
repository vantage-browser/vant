#include "agent_rpc.h"
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
namespace vantage {
namespace {
std::optional<std::string> field(std::string_view json, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    auto p=json.find(needle); if(p==std::string_view::npos)return std::nullopt;
    p=json.find(':',p+needle.size()); if(p==std::string_view::npos)return std::nullopt; ++p;
    while(p<json.size() && std::isspace(static_cast<unsigned char>(json[p])))++p;
    if(p>=json.size())return std::nullopt;
    if(json[p]=='\"') { ++p; std::string out; bool esc=false; for(;p<json.size();++p){char c=json[p]; if(esc){switch(c){case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;default:out+=c;}esc=false;}else if(c=='\\')esc=true;else if(c=='\"')return out;else out+=c;} return std::nullopt; }
    std::size_t e=p; int depth=0; bool quote=false,esc=false;
    for(;e<json.size();++e){char c=json[e]; if(quote){if(esc)esc=false;else if(c=='\\')esc=true;else if(c=='\"')quote=false;continue;} if(c=='\"'){quote=true;continue;} if(c=='{'||c=='[')++depth; else if(c=='}'||c==']'){if(depth==0)break;--depth;} else if(c==','&&depth==0)break;}
    while(e>p&&std::isspace(static_cast<unsigned char>(json[e-1])))--e;
    return std::string(json.substr(p,e-p));
}
bool parse_request(std::string_view line, AgentRequest &r) {
    auto id=field(line,"id"), method=field(line,"method"); if(!id||!method)return false;
    r.id=*id; r.method=*method; if(auto params=field(line,"params"))r.params_json=*params; return true;
}
bool write_all(int fd,std::string_view s){while(!s.empty()){auto n=::send(fd,s.data(),s.size(),MSG_NOSIGNAL);if(n<=0)return false;s.remove_prefix(static_cast<std::size_t>(n));}return true;}
std::string request_json(std::string_view id,std::string_view method,std::string_view params){return "{\"version\":1,\"id\":"+json_string(id)+",\"method\":"+json_string(method)+",\"params\":"+std::string(params)+"}\n";}
}
std::string agent_socket_path(){const char *runtime=std::getenv("XDG_RUNTIME_DIR"); std::filesystem::path base=runtime&&*runtime?runtime:"/tmp"; return (base/("vantage-agent-"+std::to_string(::getuid())+".sock")).string();}
std::string json_escape(std::string_view v){std::string o;for(unsigned char c:v){switch(c){case '\\':o+="\\\\";break;case '"':o+="\\\"";break;case '\n':o+="\\n";break;case '\r':o+="\\r";break;case '\t':o+="\\t";break;default:if(c<32){char b[7];std::snprintf(b,sizeof b,"\\u%04x",c);o+=b;}else o+=static_cast<char>(c);}}return o;}
std::string json_string(std::string_view v){return "\""+json_escape(v)+"\"";}
std::string json_param_string(std::string_view j,std::string_view k){auto v=field(j,k);return v?*v:"";}
long long json_param_integer(std::string_view j,std::string_view k,long long f){auto v=field(j,k);if(!v)return f;try{return std::stoll(*v);}catch(...){return f;}}
bool json_param_bool(std::string_view j,std::string_view k,bool f){auto v=field(j,k);if(!v)return f;return *v=="true"?true:*v=="false"?false:f;}
std::string agent_ok(std::string_view id,std::string_view result){return "{\"version\":1,\"id\":"+json_string(id)+",\"ok\":true,\"result\":"+std::string(result)+"}";}
std::string agent_error(std::string_view id,std::string_view code,std::string_view message){return "{\"version\":1,\"id\":"+json_string(id)+",\"ok\":false,\"error\":{\"code\":"+json_string(code)+",\"message\":"+json_string(message)+"}}";}
struct AgentRpcServer::Impl { AgentHandler handler; std::string path{agent_socket_path()}; int fd{-1}; std::atomic<bool> running{false}; std::thread thread; explicit Impl(AgentHandler h):handler(std::move(h)){} };
AgentRpcServer::AgentRpcServer(AgentHandler h):impl_(std::make_unique<Impl>(std::move(h))){}
AgentRpcServer::~AgentRpcServer(){stop();}
const std::string &AgentRpcServer::path()const noexcept{return impl_->path;}
bool AgentRpcServer::start(std::string *error){
    if(impl_->running)return true;
    ::unlink(impl_->path.c_str()); impl_->fd=::socket(AF_UNIX,SOCK_STREAM,0); if(impl_->fd<0){if(error)*error=std::strerror(errno);return false;}
    sockaddr_un a{};a.sun_family=AF_UNIX;if(impl_->path.size()>=sizeof a.sun_path){if(error)*error="socket path too long";::close(impl_->fd);impl_->fd=-1;return false;}std::strcpy(a.sun_path,impl_->path.c_str());
    if(::bind(impl_->fd,reinterpret_cast<sockaddr*>(&a),sizeof a)!=0||::chmod(impl_->path.c_str(),0600)!=0||::listen(impl_->fd,8)!=0){if(error)*error=std::strerror(errno);::close(impl_->fd);impl_->fd=-1;::unlink(impl_->path.c_str());return false;}
    impl_->running=true; impl_->thread=std::thread([p=impl_.get()]{while(p->running){int c=::accept(p->fd,nullptr,nullptr);if(c<0){if(p->running)continue;break;}std::string line;char ch;while(line.size()<1024*1024&&::recv(c,&ch,1,0)==1&&ch!='\n')line+=ch;AgentRequest r;if(!parse_request(line,r)){write_all(c,agent_error("","invalid_request","invalid JSON-RPC request")+"\n");::close(c);continue;}std::mutex m;std::condition_variable cv;bool done=false;std::string reply;p->handler(std::move(r),[&](std::string x){{std::lock_guard lk(m);reply=std::move(x);done=true;}cv.notify_one();});{std::unique_lock lk(m);cv.wait_for(lk,std::chrono::seconds(30),[&]{return done;});}if(!done)reply=agent_error("","timeout","request timed out");write_all(c,reply+"\n");::close(c);}}); return true;
}
void AgentRpcServer::stop(){if(!impl_||!impl_->running.exchange(false))return;::shutdown(impl_->fd,SHUT_RDWR);::close(impl_->fd);impl_->fd=-1;if(impl_->thread.joinable())impl_->thread.join();::unlink(impl_->path.c_str());}
int run_agent_cli(const std::vector<std::string_view>&a){if(a.empty()){std::cerr<<"usage: vant agent <status|version|capabilities|call> ...\n";return 2;}std::string method(a[0]),params="{}";if(method=="call"){if(a.size()<2){std::cerr<<"vant agent call: method required\n";return 2;}method=std::string(a[1]);if(a.size()>2)params=std::string(a[2]);}int fd=::socket(AF_UNIX,SOCK_STREAM,0);sockaddr_un u{};u.sun_family=AF_UNIX;auto path=agent_socket_path();std::strcpy(u.sun_path,path.c_str());if(fd<0||::connect(fd,reinterpret_cast<sockaddr*>(&u),sizeof u)!=0){std::cerr<<"vant agent: Vantage agent socket unavailable at "<<path<<"\n";if(fd>=0)::close(fd);return 1;}auto q=request_json("cli",method,params);if(!write_all(fd,q)){::close(fd);return 1;}std::string out;char b[4096];ssize_t n;while((n=::recv(fd,b,sizeof b,0))>0)out.append(b,static_cast<std::size_t>(n));::close(fd);std::cout<<out;return out.find("\"ok\":true")!=std::string::npos?0:1;}
}
