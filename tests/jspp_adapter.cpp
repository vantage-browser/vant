#include "jspp_adapter.h"
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <string>
int main(){for(int i=0;i<250;++i){vantage::JsRuntime runtime;assert(std::string(runtime.version())=="0.0.0-dev");assert(std::abs(runtime.evaluate_number("1 + 2 * 3")-7.0)<0.0001);try{runtime.evaluate_number("while (true) {}");assert(false);}catch(const std::runtime_error&){}}}
