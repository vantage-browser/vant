#include "agent_rpc.h"
#include <cassert>
#include <thread>
#include <vector>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>

namespace {
bool agent_socket_is_cloexec(const std::string &path) {
    for (const auto &entry : std::filesystem::directory_iterator("/proc/self/fd")) {
        int fd = std::stoi(entry.path().filename());
        sockaddr_un addr{};
        socklen_t len = sizeof addr;
        if (getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len) != 0) continue;
        if (addr.sun_family != AF_UNIX) continue;
        if (path != addr.sun_path) continue;
        const int flags = fcntl(fd, F_GETFD);
        if (flags < 0) return false;
        return (flags & FD_CLOEXEC) != 0;
    }
    return false;
}
}

int main(){
  auto headers=vantage::json_param_string_object(R"({"headers":{"Authorization":"Bearer token","X-Test":"yes"}})","headers");
  assert(headers.size()==2); assert(headers[0].first=="Authorization"); assert(headers[0].second=="Bearer token"); assert(headers[1].first=="X-Test"); assert(headers[1].second=="yes");
  vantage::AgentEventLog log(2);
  assert(log.publish("one","{\"n\":1}")==1);
  assert(log.publish("two")==2);
  assert(log.publish("three")==3);
  assert(log.dropped()==1);
  auto e=log.since(1,10); assert(e.size()==2); assert(e[0].sequence==2); assert(e[1].type=="three");
  log.clear(); assert(log.since(0).empty()); assert(log.dropped()==0);
  vantage::AgentEventLog concurrent(1000); std::vector<std::thread> workers;
  for(int i=0;i<8;i++) workers.emplace_back([&]{for(int j=0;j<100;j++) concurrent.publish("tick");});
  for(auto &w:workers) w.join();
  assert(concurrent.latest()==800);
  assert(concurrent.since(0,1000).size()==800);
  auto runtime=std::filesystem::temp_directory_path()/"vantage-agent-rpc-test"; std::filesystem::create_directories(runtime);
  setenv("XDG_RUNTIME_DIR",runtime.c_str(),1);
  std::string socket; { vantage::AgentRpcServer server([](vantage::AgentRequest r,vantage::AgentReply reply){reply(vantage::agent_ok(r.id,"true"));}); std::string error; assert(server.start(&error)); socket=server.path(); assert(std::filesystem::exists(socket));
    assert(agent_socket_is_cloexec(socket) && "agent listening socket must not be inherited by web/child processes"); }
  assert(!std::filesystem::exists(socket)); std::filesystem::remove_all(runtime);
}
