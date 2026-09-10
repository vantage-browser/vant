#include "jspp_adapter.h"
#include <js.h>
#include <stdexcept>
namespace vantage {
JsRuntime::JsRuntime():runtime_(js_runtime_new()){if(!runtime_)throw std::runtime_error("create JS++ runtime");js_runtime_set_instruction_limit(runtime_,100000);js_runtime_set_stack_limit(runtime_,128);js_runtime_set_allocation_limit(runtime_,10000);}
JsRuntime::~JsRuntime(){js_runtime_free(runtime_);}
double JsRuntime::evaluate_number(const std::string &source){js_value*value=nullptr;const auto status=js_eval(runtime_,source.c_str(),&value);if(status!=JS_STATUS_OK)throw std::runtime_error(js_runtime_last_error(runtime_));double result=0;if(!js_value_get_number(value,&result)){js_value_free(runtime_,value);throw std::runtime_error("script did not return a number");}js_value_free(runtime_,value);return result;}
const char *JsRuntime::version()const noexcept{return js_version();}
}
