#include "agent_rpc.h"
#include <cassert>
#include <thread>
#include <vector>
#include <filesystem>
#include <cstdlib>
int main(){
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
  std::string socket; { vantage::AgentRpcServer server([](vantage::AgentRequest r,vantage::AgentReply reply){reply(vantage::agent_ok(r.id,"true"));}); std::string error; assert(server.start(&error)); socket=server.path(); assert(std::filesystem::exists(socket)); }
  assert(!std::filesystem::exists(socket)); std::filesystem::remove_all(runtime);
}
