#include "agent_rpc.h"
#include <cassert>
int main(){
  vantage::AgentEventLog log(2);
  assert(log.publish("one","{\"n\":1}")==1);
  assert(log.publish("two")==2);
  assert(log.publish("three")==3);
  assert(log.dropped()==1);
  auto e=log.since(1,10); assert(e.size()==2); assert(e[0].sequence==2); assert(e[1].type=="three");
  log.clear(); assert(log.since(0).empty()); assert(log.dropped()==0);
}
