#include "agent_rpc.h"
#include <chrono>
#include <iostream>
int main(){
  using clock=std::chrono::steady_clock;
  vantage::AgentEventLog log(2048);
  constexpr int rounds=200000;
  auto start=clock::now(); for(int i=0;i<rounds;i++) log.publish("benchmark","{\"tab_id\":1}"); auto publish_end=clock::now();
  std::uint64_t cursor=0; std::size_t read=0; auto read_start=clock::now();
  for(int i=0;i<1000;i++){auto events=log.since(cursor,256);if(!events.empty())cursor=events.back().sequence;read+=events.size();}
  auto end=clock::now();
  auto pub_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(publish_end-start).count()/double(rounds);
  auto read_us=std::chrono::duration_cast<std::chrono::microseconds>(end-read_start).count()/1000.0;
  std::cout<<"agent_event_publish_ns="<<pub_ns<<"\nagent_event_poll_us="<<read_us<<"\nretained="<<log.since(0,4096).size()<<"\ndropped="<<log.dropped()<<"\nread="<<read<<"\n";
}
