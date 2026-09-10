#include "automation.h"
#include <js.h>
#include <stdexcept>
namespace {
js_status fail(js_runtime*r,const char*message){js_runtime_set_error(r,message);return JS_STATUS_RUNTIME_ERROR;}
vantage::AutomationEngine *host(void*data){return static_cast<vantage::AutomationEngine*>(data);}
js_status tab_count(js_runtime*r,const js_value*,const js_value*const*,size_t count,void*data,js_value**result){auto*h=host(data);if(count)return fail(r,"tabCount expects no arguments");if(h->cancelled())return fail(r,"automation cancelled");if(!h->granted(vantage::Capability::tabs_read))return fail(r,"capability denied: tabs.read");return js_value_new_number(r,static_cast<double>(h->model().tabs().size()),result);}
js_status open_tab(js_runtime*r,const js_value*,const js_value*const*args,size_t count,void*data,js_value**result){auto*h=host(data);if(h->cancelled())return fail(r,"automation cancelled");if(!h->granted(vantage::Capability::tabs_write))return fail(r,"capability denied: tabs.write");if(count!=1)return fail(r,"openTab expects one URI");const char*text=nullptr;size_t size=0;if(!js_value_get_string(args[0],&text,&size))return fail(r,"openTab URI must be a string");const auto decision=h->navigation().resolve(std::string(text,size));if(decision.kind!=vantage::NavigationKind::web&&decision.kind!=vantage::NavigationKind::internal)return fail(r,"openTab URI rejected");h->model().new_tab(decision.uri);return js_value_new_number(r,static_cast<double>(*h->model().active_tab()),result);}
js_status bookmark(js_runtime*r,const js_value*,const js_value*const*args,size_t count,void*data,js_value**result){auto*h=host(data);if(h->cancelled())return fail(r,"automation cancelled");if(!h->granted(vantage::Capability::bookmarks_write))return fail(r,"capability denied: bookmarks.write");if(count!=2)return fail(r,"bookmark expects URI and title");const char*uri=nullptr;const char*title=nullptr;size_t uri_size=0,title_size=0;if(!js_value_get_string(args[0],&uri,&uri_size)||!js_value_get_string(args[1],&title,&title_size))return fail(r,"bookmark arguments must be strings");const auto decision=h->navigation().resolve(std::string(uri,uri_size));if(decision.kind!=vantage::NavigationKind::web)return fail(r,"bookmark URI rejected");h->data().add_bookmark({decision.uri,std::string(title,title_size)});return js_value_new_number(r,1,result);}
void install(js_runtime*r,js_value*object,const char*name,js_native_callback callback,void*data){js_value*function=nullptr;if(js_function_new_native(r,name,std::char_traits<char>::length(name),callback,data,&function)!=JS_STATUS_OK)throw std::runtime_error("create automation function");const auto status=js_object_set(r,object,name,std::char_traits<char>::length(name),function);js_value_free(r,function);if(status!=JS_STATUS_OK)throw std::runtime_error("install automation function");}
}
namespace vantage {
AutomationEngine::AutomationEngine(BrowserModel&model,UserDataStore&data,std::set<Capability>grants):model_(model),data_(data),grants_(std::move(grants)),runtime_(js_runtime_new()){
 if(!runtime_) throw std::runtime_error("create automation runtime");
 js_runtime_set_instruction_limit(runtime_,100000);
 js_runtime_set_stack_limit(runtime_,128);
 js_runtime_set_allocation_limit(runtime_,10000);
 js_value*object=nullptr;
 try{if(js_object_new(runtime_,&object)!=JS_STATUS_OK)throw std::runtime_error("create vant API");install(runtime_,object,"tabCount",tab_count,this);install(runtime_,object,"openTab",open_tab,this);install(runtime_,object,"bookmark",bookmark,this);if(js_runtime_set_global(runtime_,"vant",4,object)!=JS_STATUS_OK)throw std::runtime_error("publish vant API");js_value_free(runtime_,object);}catch(...){if(object)js_value_free(runtime_,object);js_runtime_free(runtime_);runtime_=nullptr;throw;}
}
AutomationEngine::~AutomationEngine(){js_runtime_free(runtime_);}
double AutomationEngine::evaluate_number(const std::string&source){js_value*value=nullptr;const auto status=js_eval(runtime_,source.c_str(),&value);if(status!=JS_STATUS_OK)throw std::runtime_error(js_runtime_last_error(runtime_));double number=0;if(!js_value_get_number(value,&number)){js_value_free(runtime_,value);throw std::runtime_error("automation result is not a number");}js_value_free(runtime_,value);return number;}
}
