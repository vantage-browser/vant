#include "agent_rpc.h"
#include <atomic>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <fcntl.h>
#include <thread>
#include <unistd.h>
namespace vantage {
namespace {
bool set_cloexec(int fd) {
    const int flags = fcntl(fd, F_GETFD);
    if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) return false;
    return true;
}
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
char json_value_kind(std::string_view json,std::string_view key){
    const std::string needle = "\"" + std::string(key) + "\"";
    auto p=json.find(needle); if(p==std::string_view::npos) return '?';
    p=json.find(':',p+needle.size()); if(p==std::string_view::npos) return '?'; ++p;
    while(p<json.size()&&std::isspace(static_cast<unsigned char>(json[p])))++p;
    if(p>=json.size()) return '?';
    const char c=json[p];
    if(c=='"') return 's';
    if(c=='t'||c=='f') return 'b';
    if(c=='-'||(c>='0'&&c<='9')) return 'n';
    return '?';
}
std::vector<std::pair<std::string,std::string>> json_param_string_object(std::string_view json,std::string_view key){
    std::vector<std::pair<std::string,std::string>> out;
    auto raw=field(json,key); if(!raw||raw->size()<2||raw->front()!='{'||raw->back()!='}') return out;
    std::size_t p=1;
    auto skip=[&]{while(p<raw->size()&&std::isspace(static_cast<unsigned char>((*raw)[p])))++p;};
    auto string_value=[&]()->std::optional<std::string>{
        skip(); if(p>=raw->size()||(*raw)[p]!='"')return std::nullopt; ++p; std::string value; bool esc=false;
        for(;p<raw->size();++p){char c=(*raw)[p]; if(esc){switch(c){case 'n':value+='\n';break;case 'r':value+='\r';break;case 't':value+='\t';break;default:value+=c;}esc=false;}else if(c=='\\')esc=true;else if(c=='"'){++p;return value;}else value+=c;} return std::nullopt;
    };
    skip(); if(p<raw->size()&&(*raw)[p]=='}')return out;
    while(p<raw->size()){
        auto k=string_value(); if(!k)return {}; skip(); if(p>=raw->size()||(*raw)[p++]!=':')return {};
        auto v=string_value(); if(!v)return {}; out.emplace_back(std::move(*k),std::move(*v)); skip();
        if(p<raw->size()&&(*raw)[p]==','){++p;continue;} if(p<raw->size()&&(*raw)[p]=='}')break; return {};
    }
    return out;
}

std::string agent_ok(std::string_view id,std::string_view result){return "{\"version\":1,\"id\":"+json_string(id)+",\"ok\":true,\"result\":"+std::string(result)+"}";}
std::string agent_error(std::string_view id,std::string_view code,std::string_view message){return "{\"version\":1,\"id\":"+json_string(id)+",\"ok\":false,\"error\":{\"code\":"+json_string(code)+",\"message\":"+json_string(message)+"}}";}
AgentEventLog::AgentEventLog(std::size_t capacity):capacity_(std::max<std::size_t>(1,capacity)){}
std::uint64_t AgentEventLog::publish(std::string type,std::string payload){std::lock_guard lk(mutex_);auto seq=next_++;if(events_.size()>=capacity_){events_.pop_front();++dropped_;}events_.push_back({seq,std::move(type),std::move(payload)});return seq;}
std::vector<AgentEvent> AgentEventLog::since(std::uint64_t after,std::size_t limit)const{std::lock_guard lk(mutex_);std::vector<AgentEvent> out;for(const auto&e:events_)if(e.sequence>after){out.push_back(e);if(out.size()>=limit)break;}return out;}
std::uint64_t AgentEventLog::latest()const{std::lock_guard lk(mutex_);return next_-1;}
std::uint64_t AgentEventLog::dropped()const{std::lock_guard lk(mutex_);return dropped_;}
void AgentEventLog::clear(){std::lock_guard lk(mutex_);events_.clear();dropped_=0;}
struct AgentRpcServer::Impl { AgentHandler handler; std::string path{agent_socket_path()}; int fd{-1}; std::atomic<bool> running{false}; std::thread thread; std::mutex clients_mutex; std::vector<std::thread> clients; explicit Impl(AgentHandler h):handler(std::move(h)){} };
AgentRpcServer::AgentRpcServer(AgentHandler h):impl_(std::make_unique<Impl>(std::move(h))){}
AgentRpcServer::~AgentRpcServer(){stop();}
const std::string &AgentRpcServer::path()const noexcept{return impl_->path;}
bool AgentRpcServer::start(std::string *error){
    if(impl_->running)return true;
    ::unlink(impl_->path.c_str()); impl_->fd=::socket(AF_UNIX,SOCK_STREAM,0); if(impl_->fd<0){if(error)*error=std::strerror(errno);return false;}
    if(!set_cloexec(impl_->fd)){if(error)*error="failed to set close-on-exec on agent socket";::close(impl_->fd);impl_->fd=-1;return false;}
    sockaddr_un a{};a.sun_family=AF_UNIX;if(impl_->path.size()>=sizeof a.sun_path){if(error)*error="socket path too long";::close(impl_->fd);impl_->fd=-1;return false;}std::strcpy(a.sun_path,impl_->path.c_str());
    if(::bind(impl_->fd,reinterpret_cast<sockaddr*>(&a),sizeof a)!=0||::chmod(impl_->path.c_str(),0600)!=0||::listen(impl_->fd,32)!=0){if(error)*error=std::strerror(errno);::close(impl_->fd);impl_->fd=-1;::unlink(impl_->path.c_str());return false;}
    impl_->running=true;
    impl_->thread=std::thread([p=impl_.get()]{while(p->running){int c=::accept(p->fd,nullptr,nullptr);if(c<0){if(p->running)continue;break;}set_cloexec(c);std::lock_guard guard(p->clients_mutex);p->clients.emplace_back([p,c]{std::string line;char ch;bool too_large=false;while(::recv(c,&ch,1,0)==1&&ch!='\n'){if(line.size()>=1024*1024){too_large=true;break;}line+=ch;}if(too_large){write_all(c,agent_error("","request_too_large","request exceeds 1 MiB limit")+"\n");::close(c);return;}AgentRequest r;if(!parse_request(line,r)){write_all(c,agent_error("","invalid_request","invalid JSON-RPC request")+"\n");::close(c);return;}const std::string request_id=r.id;const std::string request_method=r.method;std::mutex m;std::condition_variable cv;bool done=false;std::string reply;p->handler(std::move(r),[&](std::string x){{std::lock_guard lk(m);reply=std::move(x);done=true;}cv.notify_one();});{std::unique_lock lk(m);cv.wait_for(lk,std::chrono::seconds(30),[&]{return done;});}if(!done)reply=agent_error(request_id,"timeout","request timed out for method "+request_method);write_all(c,reply+"\n");::close(c);});}});
    return true;
}
void AgentRpcServer::stop(){if(!impl_||!impl_->running.exchange(false))return;::shutdown(impl_->fd,SHUT_RDWR);::close(impl_->fd);impl_->fd=-1;if(impl_->thread.joinable())impl_->thread.join();{std::lock_guard guard(impl_->clients_mutex);for(auto &client:impl_->clients)if(client.joinable())client.join();impl_->clients.clear();}::unlink(impl_->path.c_str());}
int run_agent_cli(const std::vector<std::string_view>&input){
    if(input.empty()){std::cerr<<"usage: vant agent [--json] <status|version|capabilities|tabs|open|snapshot|click|fill|fetch|diagnostics|events|watch|call|js> ...\n";return 2;}
    std::vector<std::string_view> a=input; bool json_output=false;
    if(!a.empty()&&a[0]=="--json"){json_output=true;a.erase(a.begin());}
    if(a.empty())return 2;
    if(a[0]=="--help"||a[0]=="help"){std::cout<<"vant agent [--json] status|version|capabilities|tabs|open URI [TAB]|snapshot [TAB]|click REF [TAB]|fill REF VALUE [TAB]|fetch URL [OPTIONS_JSON]|diagnostics [TAB]|events|watch|call METHOD [JSON]|js [--file FILE|-|SCRIPT]\n";return 0;}
    auto exchange=[](std::string_view method,std::string_view params)->std::optional<std::string>{int fd=::socket(AF_UNIX,SOCK_STREAM,0);sockaddr_un u{};u.sun_family=AF_UNIX;auto path=agent_socket_path();std::strcpy(u.sun_path,path.c_str());if(fd<0||::connect(fd,reinterpret_cast<sockaddr*>(&u),sizeof u)!=0){std::cerr<<"vant agent: Vantage agent socket unavailable at "<<path<<"\n";if(fd>=0)::close(fd);return std::nullopt;}auto q=request_json("cli",method,params);if(!write_all(fd,q)){::close(fd);return std::nullopt;}std::string out;char b[4096];ssize_t n;while((n=::recv(fd,b,sizeof b,0))>0)out.append(b,static_cast<std::size_t>(n));::close(fd);return out;};
    if(a[0]=="watch"){
        std::uint64_t after=0;
        for(;;){auto out=exchange("events.since","{\"after\":"+std::to_string(after)+",\"limit\":256}");if(!out)return 1;if(out->find("\"ok\":true")==std::string::npos){std::cout<<*out;return 1;}if(auto result=field(*out,"result")){std::cout<<*result<<'\n';if(auto latest=field(*result,"latest")){try{after=std::stoull(*latest);}catch(...){}}}std::this_thread::sleep_for(std::chrono::milliseconds(500));}
    }
    std::string method(a[0]),params="{}";
    if(method=="events")method="events.since";
    else if(method=="tabs")method="browser.tabs";
    else if(method=="open") { if(a.size()<2){std::cerr<<"vant agent open: URI required\n";return 2;} method="browser.navigate";params="{\"uri\":"+json_string(a[1])+(a.size()>2?",\"tab_id\":"+std::string(a[2]):"")+"}"; }
    else if(method=="snapshot") { method="page.snapshot"; if(a.size()>1)params="{\"tab_id\":"+std::string(a[1])+"}"; }
    else if(method=="click") { if(a.size()<2){std::cerr<<"vant agent click: element ref required\n";return 2;}method="page.interact";params="{\"ref\":"+json_string(a[1])+",\"action\":\"click\""+(a.size()>2?",\"tab_id\":"+std::string(a[2]):"")+"}"; }
    else if(method=="fill") { if(a.size()<3){std::cerr<<"vant agent fill: element ref and value required\n";return 2;}method="page.interact";params="{\"ref\":"+json_string(a[1])+",\"action\":\"fill\",\"value\":"+json_string(a[2])+(a.size()>3?",\"tab_id\":"+std::string(a[3]):"")+"}"; }
    else if(method=="fetch") {
        if(a.size()<2){std::cerr<<"vant agent fetch: URL required\n";return 2;}
        method="net.fetch";
        std::string options=a.size()>2?std::string(a[2]):"{}";
        if(options.size()<2||options.front()!='{'||options.back()!='}'){std::cerr<<"vant agent fetch: OPTIONS_JSON must be a JSON object\n";return 2;}
        auto inner=options.substr(1,options.size()-2);
        params="{\"url\":"+json_string(a[1])+(inner.empty()?"":","+inner)+"}";
    }
    else if(method=="diagnostics") { method="page.diagnostics"; if(a.size()>1)params="{\"tab_id\":"+std::string(a[1])+"}"; }
    else if(method=="js"){std::string script;if(a.size()>=3&&a[1]=="--file"){std::ifstream in{std::string(a[2])};if(!in){std::cerr<<"vant agent js: cannot read script file\n";return 2;}script.assign(std::istreambuf_iterator<char>(in),{});}else if(a.size()>=2&&a[1]!="-")script=std::string(a[1]);else script.assign(std::istreambuf_iterator<char>(std::cin),{});method="page.javascript";params="{\"script\":"+json_string(script)+"}";}
    else if(method=="call"){if(a.size()<2){std::cerr<<"vant agent call: method required\n";return 2;}method=std::string(a[1]);if(a.size()>2)params=std::string(a[2]);}
    auto out=exchange(method,params);if(!out)return 1;const bool ok=out->find("\"ok\":true")!=std::string::npos;if(json_output||!ok)std::cout<<*out;else if(auto result=field(*out,"result"))std::cout<<*result<<'\n';else std::cout<<*out;return ok?0:1;
}
}
