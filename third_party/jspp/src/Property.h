#pragma once
#include "VM.h"
#include <algorithm>

namespace jspp {
struct PropertyMaps {
    std::unordered_map<std::string, js_value>* values = nullptr;
    std::unordered_map<std::string, PropertyAttributes>* attributes = nullptr;
    explicit operator bool() const { return values && attributes; }
};

inline PropertyMaps own_property_maps(const js_value& value) {
    if (value.kind == JS_VALUE_OBJECT && value.object)
        return {&value.object->properties, &value.object->attributes};
    if (value.kind == JS_VALUE_ARRAY && value.array)
        return {&value.array->properties, &value.array->attributes};
    if (value.kind == JS_VALUE_FUNCTION && value.function)
        return {&value.function->properties, &value.function->attributes};
    return {};
}

inline bool own_property(const PropertyMaps& maps, const std::string& name,
                         js_value& value, PropertyAttributes& attributes) {
    if (!maps) return false;
    const auto item = maps.values->find(name);
    const auto flags = maps.attributes->find(name);
    if (item == maps.values->end() && flags == maps.attributes->end()) return false;
    value = item == maps.values->end() ? js_value{} : item->second;
    attributes = flags == maps.attributes->end() ? PropertyAttributes{} : flags->second;
    return true;
}

inline void define_own_property(const PropertyMaps& maps, const std::string& name,
                                js_value value, PropertyAttributes attributes = {}) {
    (*maps.values)[name] = std::move(value);
    (*maps.attributes)[name] = std::move(attributes);
}

inline std::vector<std::string> own_property_keys(const PropertyMaps& maps,
                                                  bool enumerable_only) {
    std::vector<std::string> keys;
    if (!maps) return keys;
    for (const auto& item : *maps.values) {
        const auto flags = maps.attributes->find(item.first);
        if (!enumerable_only || flags == maps.attributes->end() || flags->second.enumerable)
            keys.push_back(item.first);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

inline bool named_property_get(const js_value& receiver, const std::string& name,
                               const NativeInvoke& invoke, js_value& out) {
    auto read = [&](const PropertyMaps& maps) {
        js_value stored; PropertyAttributes flags;
        if (!own_property(maps, name, stored, flags)) return false;
        if (!flags.accessor) { out = stored; return true; }
        if (flags.getter.kind != JS_VALUE_FUNCTION) { out = {}; return true; }
        return invoke(flags.getter, {}, receiver, false, out);
    };
    if (read(own_property_maps(receiver))) return true;
    std::shared_ptr<ObjectValue> prototype;
    if (receiver.kind == JS_VALUE_OBJECT && receiver.object) prototype = receiver.object->prototype;
    else if (receiver.kind == JS_VALUE_ARRAY && receiver.array) prototype = receiver.array->prototype;
    else if (receiver.kind == JS_VALUE_FUNCTION && receiver.function) prototype = receiver.function->object_prototype;
    for (auto at = prototype; at; at = at->prototype) {
        js_value holder; holder.kind = JS_VALUE_OBJECT; holder.object = at;
        if (read(own_property_maps(holder))) return true;
    }
    return false;
}

inline bool named_property_set(const js_value& receiver, const std::string& name,
                               const js_value& value, const NativeInvoke& invoke,
                               std::string& error) {
    auto maps = own_property_maps(receiver);
    if (!maps) return false;
    js_value stored; PropertyAttributes flags;
    if (own_property(maps, name, stored, flags)) {
        if (flags.accessor) {
            if (flags.setter.kind != JS_VALUE_FUNCTION) {
                error = "cannot assign to getter-only property '" + name + "'";
                return false;
            }
            js_value ignored;
            return invoke(flags.setter, {value}, receiver, false, ignored);
        }
        if (!flags.writable) {
            error = "cannot assign to read-only property '" + name + "'";
            return false;
        }
    }
    (*maps.values)[name] = value;
    return true;
}
}
