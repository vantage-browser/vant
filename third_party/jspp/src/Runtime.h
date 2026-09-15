#pragma once
#include "js.h"
#include <string>
#include <memory>
#include <thread>
#include <unordered_set>
#include <vector>

namespace jspp { struct FunctionObject;struct ObjectValue;struct ArrayValue;struct Environment;class Heap; }
struct js_value { js_value_kind kind=JS_VALUE_UNDEFINED; bool boolean=false; double number=0; std::string string; std::shared_ptr<jspp::FunctionObject> function;std::shared_ptr<jspp::ObjectValue> object;std::shared_ptr<jspp::ArrayValue> array;std::shared_ptr<jspp::Heap> heap; };
struct js_runtime {
 std::thread::id owner=std::this_thread::get_id();
 std::string error;
 std::unordered_set<js_value*> values;
 std::shared_ptr<jspp::Heap> heap;
 std::shared_ptr<jspp::Environment> global;
 std::vector<js_value> intrinsic_roots;
 std::size_t evaluation_depth=0;
 std::size_t host_sequence=0;
 js_value last_exception;
 bool has_exception=false;
 std::size_t instruction_limit=1000000;
 std::size_t stack_limit=512;
};
