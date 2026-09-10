#include "VM.h"
#include <algorithm>
#include <functional>
#include <unordered_set>
namespace jspp {
std::shared_ptr<Environment>Heap::environment(){if(allocation_limit_&&allocations_>=allocation_limit_)throw std::bad_alloc();auto v=std::make_shared<Environment>();environments_.push_back(v);++allocations_;return v;}
std::shared_ptr<FunctionObject>Heap::function(){if(allocation_limit_&&allocations_>=allocation_limit_)throw std::bad_alloc();auto v=std::make_shared<FunctionObject>();functions_.push_back(v);++allocations_;return v;}
std::shared_ptr<ObjectValue>Heap::object(){if(allocation_limit_&&allocations_>=allocation_limit_)throw std::bad_alloc();auto v=std::make_shared<ObjectValue>();objects_.push_back(v);++allocations_;return v;}
std::shared_ptr<ArrayValue>Heap::array(){if(allocation_limit_&&allocations_>=allocation_limit_)throw std::bad_alloc();auto v=std::make_shared<ArrayValue>();arrays_.push_back(v);++allocations_;return v;}
std::size_t Heap::tracked()const{std::size_t n=0;for(const auto&v:environments_)n+=!v.expired();for(const auto&v:functions_)n+=!v.expired();for(const auto&v:objects_)n+=!v.expired();for(const auto&v:arrays_)n+=!v.expired();return n;}
std::size_t Heap::collect(const std::vector<js_value>&roots,const std::vector<std::shared_ptr<Environment>>&environment_roots){
 if(collecting_)return 0;
 collecting_=true;
 std::vector<std::shared_ptr<Environment>>envs;std::vector<std::shared_ptr<FunctionObject>>funcs;std::vector<std::shared_ptr<ObjectValue>>objs;std::vector<std::shared_ptr<ArrayValue>>arrs;
 for(auto&v:environments_)if(auto x=v.lock())envs.push_back(x);
 for(auto&v:functions_)if(auto x=v.lock())funcs.push_back(x);
 for(auto&v:objects_)if(auto x=v.lock())objs.push_back(x);
 for(auto&v:arrays_)if(auto x=v.lock())arrs.push_back(x);
 std::unordered_set<const void*>marked;std::function<void(const js_value&)>value;std::function<void(const std::shared_ptr<Environment>&)>env;std::function<void(const std::shared_ptr<ObjectValue>&)>obj;std::function<void(const std::shared_ptr<ArrayValue>&)>arr;std::function<void(const std::shared_ptr<FunctionObject>&)>fun;
 env=[&](const auto&x){if(!x||!marked.insert(x.get()).second)return;env(x->parent);for(const auto&entry:x->bindings)value(entry.second.value);};
 obj=[&](const auto&x){if(!x||!marked.insert(x.get()).second)return;obj(x->prototype);for(const auto&entry:x->properties)value(entry.second);for(const auto&entry:x->attributes){value(entry.second.getter);value(entry.second.setter);}};
 arr=[&](const auto&x){if(!x||!marked.insert(x.get()).second)return;obj(x->prototype);for(const auto&v:x->elements)value(v);for(const auto&entry:x->properties)value(entry.second);for(const auto&entry:x->attributes){value(entry.second.getter);value(entry.second.setter);}};
 fun=[&](const auto&x){if(!x||!marked.insert(x.get()).second)return;env(x->closure);obj(x->instance_prototype);obj(x->object_prototype);for(const auto&entry:x->properties)value(entry.second);for(const auto&entry:x->attributes){value(entry.second.getter);value(entry.second.setter);}};
 value=[&](const js_value&v){if(v.kind==JS_VALUE_FUNCTION)fun(v.function);else if(v.kind==JS_VALUE_OBJECT)obj(v.object);else if(v.kind==JS_VALUE_ARRAY)arr(v.array);};for(const auto&root:roots)value(root);for(const auto&root:environment_roots)env(root);
 std::size_t reclaimed=0;for(auto&x:envs)if(!marked.count(x.get())){x->bindings.clear();x->parent.reset();++reclaimed;}for(auto&x:funcs)if(!marked.count(x.get())){x->properties.clear();x->attributes.clear();x->closure.reset();x->instance_prototype.reset();x->object_prototype.reset();x->native={};++reclaimed;}for(auto&x:objs)if(!marked.count(x.get())){x->properties.clear();x->attributes.clear();x->prototype.reset();++reclaimed;}for(auto&x:arrs)if(!marked.count(x.get())){x->elements.clear();x->properties.clear();x->attributes.clear();x->prototype.reset();++reclaimed;}
 auto compact=[](auto&items){items.erase(std::remove_if(items.begin(),items.end(),[](const auto&item){return item.expired();}),items.end());};compact(environments_);compact(functions_);compact(objects_);compact(arrays_);next_collection_=allocations_+64;++collections_;collecting_=false;return reclaimed;
}
}
