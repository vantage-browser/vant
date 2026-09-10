#pragma once
#include "VM.h"

namespace jspp {
inline std::string source_frame(const std::string& name, std::size_t line, std::size_t column) {
    auto frame=name.empty()?std::string("<anonymous>"):name;
    if(line)frame+=" (<eval>:"+std::to_string(line)+":"+std::to_string(column)+")";
    return frame;
}
inline js_value error_object(Heap& heap, const std::string& name,
                             const std::string& message,
                             const std::shared_ptr<ObjectValue>& prototype = {},
                             const std::vector<std::string>& frames = {}) {
    js_value thrown; thrown.kind = JS_VALUE_OBJECT; thrown.object = heap.object();
    thrown.object->prototype = prototype;
    auto string = [](std::string text) { js_value value; value.kind = JS_VALUE_STRING; value.string = std::move(text); return value; };
    thrown.object->properties["name"] = string(name);
    thrown.object->properties["message"] = string(message);
    std::string stack = name + (message.empty() ? "" : ": " + message);
    for (const auto& frame : frames) stack += "\n    at " + (frame.empty() ? "<anonymous>" : frame);
    thrown.object->properties["stack"] = string(std::move(stack));
    return thrown;
}

inline void append_error_frame(js_value& thrown, const std::string& frame) {
    if (thrown.kind != JS_VALUE_OBJECT || !thrown.object) return;
    auto stack = thrown.object->properties.find("stack");
    if (stack == thrown.object->properties.end() || stack->second.kind != JS_VALUE_STRING) return;
    stack->second.string += "\n    at " + (frame.empty() ? "<anonymous>" : frame);
}
}
