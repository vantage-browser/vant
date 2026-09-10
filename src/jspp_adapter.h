#ifndef VANTAGE_JSPP_ADAPTER_H
#define VANTAGE_JSPP_ADAPTER_H
#include <cstddef>
#include <string>
struct js_runtime;
namespace vantage {
class JsRuntime {
public:
    JsRuntime();~JsRuntime();JsRuntime(const JsRuntime&)=delete;JsRuntime&operator=(const JsRuntime&)=delete;
    double evaluate_number(const std::string &source);
    const char *version() const noexcept;
private: js_runtime *runtime_{};
};
}
#endif
