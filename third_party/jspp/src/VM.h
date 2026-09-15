#pragma once
#include "Bytecode.h"
#include "Runtime.h"
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
namespace jspp {
struct Binding { js_value value;bool constant=false; };
struct PropertyAttributes { bool writable=true;bool enumerable=true;bool configurable=true;bool accessor=false;js_value getter;js_value setter; };
struct Environment { std::unordered_map<std::string,Binding>bindings;std::shared_ptr<Environment>parent; };
class Heap;
using NativeInvoke=std::function<bool(const js_value&,const std::vector<js_value>&,const js_value&,bool,js_value&)>;
using NativeFunction=std::function<bool(const std::vector<js_value>&,const js_value&,bool,js_value&,std::string&,Heap&,const NativeInvoke&)>;
struct FunctionObject { std::shared_ptr<FunctionPrototype>prototype;std::shared_ptr<Environment>closure;std::unordered_map<std::string,js_value>properties;std::unordered_map<std::string,PropertyAttributes>attributes;std::shared_ptr<ObjectValue>instance_prototype;std::shared_ptr<ObjectValue>object_prototype;NativeFunction native;std::string name;std::size_t length=0;bool constructible=true; };
struct ObjectValue { std::unordered_map<std::string,js_value>properties;std::unordered_map<std::string,PropertyAttributes>attributes;std::shared_ptr<ObjectValue>prototype; };
struct ArrayValue { std::vector<js_value>elements;std::unordered_map<std::string,js_value>properties;std::unordered_map<std::string,PropertyAttributes>attributes;std::shared_ptr<ObjectValue>prototype; };
class Heap {
public:
 ~Heap(){collect({});}
 std::shared_ptr<Environment>environment();std::shared_ptr<FunctionObject>function();
 std::shared_ptr<ObjectValue>object();std::shared_ptr<ArrayValue>array();
 std::size_t allocations()const{return allocations_;}std::size_t tracked()const;
 std::size_t collect(const std::vector<js_value>&roots,const std::vector<std::shared_ptr<Environment>>&environment_roots={});
 bool collection_due()const{return collection_blocks_==0&&allocations_>=next_collection_;}
 void block_collection(){++collection_blocks_;}
 void unblock_collection(){if(collection_blocks_)--collection_blocks_;}
 void set_allocation_limit(std::size_t limit){allocation_limit_=limit;}
 std::size_t collections()const{return collections_;}
private:
 std::size_t allocations_=0,next_collection_=64,collections_=0,collection_blocks_=0,allocation_limit_=0;bool collecting_=false;std::vector<std::weak_ptr<Environment>>environments_;
 std::vector<std::weak_ptr<FunctionObject>>functions_;std::vector<std::weak_ptr<ObjectValue>>objects_;
 std::vector<std::weak_ptr<ArrayValue>>arrays_;
};
enum class CompletionKind { Normal,Return,Throw,Break,Continue };
struct Completion { CompletionKind kind=CompletionKind::Normal;js_value value;std::size_t target=0,target_scope=0; };
// Standalone execution lifetime contract (PC0V corrective fix):
// Ownership is a single coherent model: every execution runs against one Heap,
// and the returned completion value is only ever reclaimable while that Heap
// (or a value that pins it) is live.
// - heap!=nullptr (caller-owned heap): the caller owns every root, including any
//   supplied global and live handles. execute_completion performs no collection
//   at exit; the caller drives Heap::collect. The runtime embedding path uses
//   this rule and collects after each evaluation. global may be supplied or
//   null; when null the heap allocates a fresh top-level environment.
// - heap==nullptr && global==nullptr (call-local heap): the call owns a local
//   Heap. On every exit from run() the local heap is swept once, rooting only
//   Completion.value. A heap-backed completion value then pins the local heap
//   through js_value::heap, so a self-referential returned graph stays valid
//   and is reclaimed when the value is released (Heap teardown collection).
// - heap==nullptr && global!=nullptr is rejected deterministically: a caller-
//   owned persistent global cannot be tracked by a call-local heap, and the
//   final sweep (or the local heap disappearing) would lose or mutate live
//   externally owned state.
bool execute_completion(const Bytecode&,Completion&,std::string&error,std::size_t instruction_budget=1000000,Heap*heap=nullptr,std::shared_ptr<Environment>global={},std::size_t stack_limit=512);
bool execute(const Bytecode&,js_value&,std::string&error,std::size_t instruction_budget=1000000,Heap*heap=nullptr,std::shared_ptr<Environment>global={},std::size_t stack_limit=512);
}
