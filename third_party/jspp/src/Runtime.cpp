#include "Runtime.h"
#include "Bytecode.h"
#include "Frontend.h"
#include "VM.h"
#include "Intrinsics.h"
#include "Version.h"
#include <new>
#include <memory>
#include <limits>
#include <sstream>

extern "C" {
const char*js_version(void){return JS_VERSION;}
unsigned int js_api_version(void){return JS_API_VERSION;}
js_runtime*js_runtime_new(void){try{auto r=std::make_unique<js_runtime>();r->heap=std::make_shared<jspp::Heap>();auto intrinsics=jspp::create_intrinsics(*r->heap);r->global=std::move(intrinsics.global);r->intrinsic_roots=std::move(intrinsics.roots);return r.release();}catch(...){return nullptr;}}
static void collect(js_runtime*r){if(r->evaluation_depth)return;std::vector<js_value>roots=r->intrinsic_roots;for(auto*v:r->values)roots.push_back(*v);r->heap->collect(roots,{r->global});}
void js_runtime_free(js_runtime*r){if(!r)return;for(auto*v:r->values)delete v;r->values.clear();r->intrinsic_roots.clear();r->global.reset();r->heap->collect({});delete r;}
const char*js_runtime_last_error(const js_runtime*r){return r?r->error.c_str():"invalid runtime";}
static js_status fail(js_runtime*r,js_status s,const char*m){if(r)r->error=m;return s;}
static bool owner(js_runtime*r){return r&&r->owner==std::this_thread::get_id();}
void js_runtime_set_error(js_runtime*r,const char*message){if(r&&owner(r))r->error=message?message:"native callback failed";}
static bool owns(js_runtime*r,const js_value*v){return r&&v&&r->values.count(const_cast<js_value*>(v));}
static js_status retain(js_runtime*r,const js_value&value,js_value**out){
 if(out)*out=nullptr;
 if(!r||!out)return fail(r,JS_STATUS_INVALID_ARGUMENT,"runtime and result are required");
 if(!owner(r))return fail(r,JS_STATUS_WRONG_THREAD,"runtime used from a non-owner thread");
 try{auto handle=std::make_unique<js_value>(value);r->values.insert(handle.get());*out=handle.release();r->error.clear();return JS_STATUS_OK;}catch(const std::bad_alloc&){return fail(r,JS_STATUS_OUT_OF_MEMORY,"out of memory");}
}
js_status js_runtime_get_exception(js_runtime*r,js_value**out){if(!r||!out)return fail(r,JS_STATUS_INVALID_ARGUMENT,"runtime and result are required");if(!owner(r))return fail(r,JS_STATUS_WRONG_THREAD,"runtime used from a non-owner thread");if(!r->has_exception)return fail(r,JS_STATUS_INVALID_ARGUMENT,"no JavaScript exception is available");return retain(r,r->last_exception,out);}
js_status js_runtime_set_global(js_runtime*r,const char*name,size_t size,const js_value*value){if(!owner(r))return fail(r,r?JS_STATUS_WRONG_THREAD:JS_STATUS_INVALID_ARGUMENT,r?"runtime used from a non-owner thread":"runtime is required");if(!name||!size||!owns(r,value))return fail(r,JS_STATUS_INVALID_ARGUMENT,"name and owned value are required");try{r->global->bindings[std::string(name,size)]={*value,false};r->error.clear();collect(r);return JS_STATUS_OK;}catch(const std::bad_alloc&){return fail(r,JS_STATUS_OUT_OF_MEMORY,"out of memory");}}
js_status js_runtime_get_global(js_runtime*r,const char*name,size_t size,js_value**out){if(!owner(r))return fail(r,r?JS_STATUS_WRONG_THREAD:JS_STATUS_INVALID_ARGUMENT,r?"runtime used from a non-owner thread":"runtime is required");if(!name||!size)return fail(r,JS_STATUS_INVALID_ARGUMENT,"global name is required");auto found=r->global->bindings.find(std::string(name,size));return retain(r,found==r->global->bindings.end()?js_value{}:found->second.value,out);}
js_status js_runtime_set_instruction_limit(js_runtime*r,size_t limit){if(!r||!limit)return fail(r,JS_STATUS_INVALID_ARGUMENT,"runtime and non-zero instruction limit are required");if(!owner(r))return fail(r,JS_STATUS_WRONG_THREAD,"runtime used from a non-owner thread");r->instruction_limit=limit;r->error.clear();return JS_STATUS_OK;}
js_status js_runtime_set_stack_limit(js_runtime*r,size_t limit){if(!r||!limit)return fail(r,JS_STATUS_INVALID_ARGUMENT,"runtime and non-zero stack limit are required");if(!owner(r))return fail(r,JS_STATUS_WRONG_THREAD,"runtime used from a non-owner thread");r->stack_limit=limit;r->error.clear();return JS_STATUS_OK;}
js_status js_runtime_set_allocation_limit(js_runtime*r,size_t limit){if(!r)return JS_STATUS_INVALID_ARGUMENT;if(!owner(r))return fail(r,JS_STATUS_WRONG_THREAD,"runtime used from a non-owner thread");if(limit&&limit<r->heap->allocations())return fail(r,JS_STATUS_INVALID_ARGUMENT,"allocation limit is below the current allocation count");r->heap->set_allocation_limit(limit);r->error.clear();return JS_STATUS_OK;}
js_status js_runtime_get_allocation_count(js_runtime*r,size_t*count){if(!r||!count)return fail(r,JS_STATUS_INVALID_ARGUMENT,"runtime and count are required");if(!owner(r))return fail(r,JS_STATUS_WRONG_THREAD,"runtime used from a non-owner thread");*count=r->heap->allocations();r->error.clear();return JS_STATUS_OK;}
struct EvaluationScope{
 js_runtime*r;bool nested;
 explicit EvaluationScope(js_runtime*runtime):r(runtime),nested(runtime->evaluation_depth>0){if(nested)r->heap->block_collection();++r->evaluation_depth;}
 ~EvaluationScope(){--r->evaluation_depth;if(nested)r->heap->unblock_collection();}
};
js_status js_eval(js_runtime*r,const char*source,js_value**result){
 if(result)*result=nullptr;
 if(!r||!source||!result)return fail(r,JS_STATUS_INVALID_ARGUMENT,"runtime, source and result are required");
 if(!owner(r))return fail(r,JS_STATUS_WRONG_THREAD,"runtime used from a non-owner thread");
 try{
  jspp::Program program;jspp::Diagnostic diagnostic;
  if(!jspp::parse_source(source,program,diagnostic))return fail(r,JS_STATUS_SYNTAX_ERROR,jspp::format_diagnostic(diagnostic).c_str());
  jspp::Bytecode code;std::string error;
  if(!jspp::compile(program,code,error))return fail(r,JS_STATUS_SYNTAX_ERROR,error.c_str());
  auto value=std::make_unique<js_value>();jspp::Completion completion;bool executed=false;
  {EvaluationScope scope(r);executed=jspp::execute_completion(code,completion,error,r->instruction_limit,r->heap.get(),r->global,r->stack_limit);}
  if(!executed){if(completion.kind==jspp::CompletionKind::Throw){r->last_exception=completion.value;r->has_exception=true;}return fail(r,error=="execution limit exceeded"||error=="call stack limit exceeded"?JS_STATUS_LIMIT_EXCEEDED:JS_STATUS_RUNTIME_ERROR,error.c_str());}
  *value=completion.value;r->values.insert(value.get());r->error.clear();r->has_exception=false;*result=value.release();collect(r);return JS_STATUS_OK;
 }catch(const std::bad_alloc&){return fail(r,JS_STATUS_OUT_OF_MEMORY,"out of memory");}catch(...){return fail(r,JS_STATUS_RUNTIME_ERROR,"internal runtime failure");}
}
void js_value_free(js_runtime*r,js_value*v){if(!r||!v)return;auto it=r->values.find(v);if(it!=r->values.end()){r->values.erase(it);delete v;collect(r);}}
js_value_kind js_value_get_kind(const js_value*v){return v?v->kind:JS_VALUE_UNDEFINED;}
int js_value_get_boolean(const js_value*v,int*out){if(!v||!out||v->kind!=JS_VALUE_BOOLEAN)return 0;*out=v->boolean?1:0;return 1;}
int js_value_get_number(const js_value*v,double*out){if(!v||!out||v->kind!=JS_VALUE_NUMBER)return 0;*out=v->number;return 1;}
int js_value_get_string(const js_value*v,const char**data,size_t*size){if(!v||!data||!size||v->kind!=JS_VALUE_STRING)return 0;*data=v->string.data();*size=v->string.size();return 1;}
js_status js_value_new_undefined(js_runtime*r,js_value**out){return retain(r,{},out);}
js_status js_value_new_null(js_runtime*r,js_value**out){js_value v;v.kind=JS_VALUE_NULL;return retain(r,v,out);}
js_status js_value_new_boolean(js_runtime*r,int input,js_value**out){js_value v;v.kind=JS_VALUE_BOOLEAN;v.boolean=input!=0;return retain(r,v,out);}
js_status js_value_new_number(js_runtime*r,double input,js_value**out){js_value v;v.kind=JS_VALUE_NUMBER;v.number=input;return retain(r,v,out);}
js_status js_value_new_string(js_runtime*r,const char*data,size_t size,js_value**out){if(!data&&size)return fail(r,JS_STATUS_INVALID_ARGUMENT,"string data is required");js_value v;v.kind=JS_VALUE_STRING;try{if(data)v.string.assign(data,size);}catch(const std::bad_alloc&){return fail(r,JS_STATUS_OUT_OF_MEMORY,"out of memory");}return retain(r,v,out);}
js_status js_object_new(js_runtime*r,js_value**out){if(!r)return fail(r,JS_STATUS_INVALID_ARGUMENT,"runtime is required");if(!owner(r))return fail(r,JS_STATUS_WRONG_THREAD,"runtime used from a non-owner thread");try{js_value v;v.kind=JS_VALUE_OBJECT;v.object=r->heap->object();auto ctor=r->global->bindings.find("Object");if(ctor!=r->global->bindings.end()&&ctor->second.value.kind==JS_VALUE_FUNCTION)v.object->prototype=ctor->second.value.function->instance_prototype;return retain(r,v,out);}catch(const std::bad_alloc&){return fail(r,JS_STATUS_OUT_OF_MEMORY,"out of memory");}}
js_status js_array_new(js_runtime*r,js_value**out){if(!r)return fail(r,JS_STATUS_INVALID_ARGUMENT,"runtime is required");if(!owner(r))return fail(r,JS_STATUS_WRONG_THREAD,"runtime used from a non-owner thread");try{js_value v;v.kind=JS_VALUE_ARRAY;v.array=r->heap->array();auto ctor=r->global->bindings.find("Array");if(ctor!=r->global->bindings.end()&&ctor->second.value.kind==JS_VALUE_FUNCTION)v.array->prototype=ctor->second.value.function->instance_prototype;return retain(r,v,out);}catch(const std::bad_alloc&){return fail(r,JS_STATUS_OUT_OF_MEMORY,"out of memory");}}
js_status js_object_set(js_runtime*r,js_value*object,const char*name,size_t size,const js_value*value){if(!owner(r))return fail(r,r?JS_STATUS_WRONG_THREAD:JS_STATUS_INVALID_ARGUMENT,r?"runtime used from a non-owner thread":"runtime is required");if(!owns(r,object)||!owns(r,value)||!name||object->kind!=JS_VALUE_OBJECT||!object->object)return fail(r,JS_STATUS_INVALID_ARGUMENT,"owned object, name and value are required");try{object->object->properties[std::string(name,size)]=*value;r->error.clear();collect(r);return JS_STATUS_OK;}catch(const std::bad_alloc&){return fail(r,JS_STATUS_OUT_OF_MEMORY,"out of memory");}}
js_status js_object_get(js_runtime*r,const js_value*object,const char*name,size_t size,js_value**out){if(!owner(r))return fail(r,r?JS_STATUS_WRONG_THREAD:JS_STATUS_INVALID_ARGUMENT,r?"runtime used from a non-owner thread":"runtime is required");if(!owns(r,object)||!name||object->kind!=JS_VALUE_OBJECT||!object->object)return fail(r,JS_STATUS_INVALID_ARGUMENT,"owned object and name are required");auto it=object->object->properties.find(std::string(name,size));return retain(r,it==object->object->properties.end()?js_value{}:it->second,out);}
js_status js_array_set(js_runtime*r,js_value*array,size_t index,const js_value*value){if(!owner(r))return fail(r,r?JS_STATUS_WRONG_THREAD:JS_STATUS_INVALID_ARGUMENT,r?"runtime used from a non-owner thread":"runtime is required");if(!owns(r,array)||!owns(r,value)||array->kind!=JS_VALUE_ARRAY||!array->array)return fail(r,JS_STATUS_INVALID_ARGUMENT,"owned array and value are required");if(index>std::numeric_limits<size_t>::max()-1)return fail(r,JS_STATUS_INVALID_ARGUMENT,"array index is too large");try{if(index>=array->array->elements.size())array->array->elements.resize(index+1);array->array->elements[index]=*value;r->error.clear();collect(r);return JS_STATUS_OK;}catch(const std::bad_alloc&){return fail(r,JS_STATUS_OUT_OF_MEMORY,"out of memory");}}
js_status js_array_get(js_runtime*r,const js_value*array,size_t index,js_value**out){if(!owner(r))return fail(r,r?JS_STATUS_WRONG_THREAD:JS_STATUS_INVALID_ARGUMENT,r?"runtime used from a non-owner thread":"runtime is required");if(!owns(r,array)||array->kind!=JS_VALUE_ARRAY||!array->array)return fail(r,JS_STATUS_INVALID_ARGUMENT,"owned array is required");return retain(r,index<array->array->elements.size()?array->array->elements[index]:js_value{},out);}
js_status js_array_get_length(js_runtime*r,const js_value*array,size_t*length){if(!owner(r))return fail(r,r?JS_STATUS_WRONG_THREAD:JS_STATUS_INVALID_ARGUMENT,r?"runtime used from a non-owner thread":"runtime is required");if(!owns(r,array)||!length||array->kind!=JS_VALUE_ARRAY||!array->array)return fail(r,JS_STATUS_INVALID_ARGUMENT,"owned array and length are required");*length=array->array->elements.size();r->error.clear();return JS_STATUS_OK;}
js_status js_function_new_native(js_runtime*r,const char*name,size_t name_size,js_native_callback callback,void*user_data,js_value**out){
 if(!owner(r))return fail(r,r?JS_STATUS_WRONG_THREAD:JS_STATUS_INVALID_ARGUMENT,r?"runtime used from a non-owner thread":"runtime is required");
 if(!name||!callback)return fail(r,JS_STATUS_INVALID_ARGUMENT,"native function name and callback are required");
 try{
  js_value value;value.kind=JS_VALUE_FUNCTION;value.function=r->heap->function();value.function->name.assign(name,name_size);value.function->constructible=false;
  auto function=r->global->bindings.find("Function"),object=r->global->bindings.find("Object");
  if(function!=r->global->bindings.end()&&function->second.value.kind==JS_VALUE_FUNCTION)value.function->object_prototype=function->second.value.function->instance_prototype;
  if(object!=r->global->bindings.end()&&object->second.value.kind==JS_VALUE_FUNCTION)value.function->instance_prototype=object->second.value.function->instance_prototype;
  value.function->native=[r,callback,user_data](const std::vector<js_value>&args,const js_value&receiver,bool,js_value&result,std::string&error,jspp::Heap&,const jspp::NativeInvoke&){
   std::vector<const js_value*> borrowed;borrowed.reserve(args.size());for(const auto&arg:args)borrowed.push_back(&arg);
   js_value*returned=nullptr;const auto status=callback(r,&receiver,borrowed.data(),borrowed.size(),user_data,&returned);
   if(status!=JS_STATUS_OK){error=r->error.empty()?"native callback failed":r->error;return false;}
   if(!owns(r,returned)){error="native callback must return an owned value";return false;}
   result=*returned;r->values.erase(returned);delete returned;return true;
  };
  return retain(r,value,out);
 }catch(const std::bad_alloc&){return fail(r,JS_STATUS_OUT_OF_MEMORY,"out of memory");}
}
js_status js_call(js_runtime*r,const js_value*function,const js_value*this_value,const js_value*const*arguments,size_t count,js_value**out){
 if(out)*out=nullptr;
 if(!owner(r))return fail(r,r?JS_STATUS_WRONG_THREAD:JS_STATUS_INVALID_ARGUMENT,r?"runtime used from a non-owner thread":"runtime is required");
 if(!owns(r,function)||function->kind!=JS_VALUE_FUNCTION||!function->function||!out)return fail(r,JS_STATUS_INVALID_ARGUMENT,"owned function and result are required");
 if(this_value&&!owns(r,this_value))return fail(r,JS_STATUS_INVALID_ARGUMENT,"this value belongs to another runtime");
 for(size_t i=0;i<count;++i)if(!arguments||!owns(r,arguments[i]))return fail(r,JS_STATUS_INVALID_ARGUMENT,"argument belongs to another runtime");
 const std::string prefix="__jspp_host_"+std::to_string(++r->host_sequence)+"_";std::vector<std::string>names;
 auto bind=[&](const std::string&name,const js_value&value){names.push_back(name);r->global->bindings[name]={value,true};};
 bind(prefix+"fn",*function);bind(prefix+"this",this_value?*this_value:js_value{});for(size_t i=0;i<count;++i)bind(prefix+"a"+std::to_string(i),*arguments[i]);
 std::ostringstream source;source<<prefix<<"fn.call("<<prefix<<"this";for(size_t i=0;i<count;++i)source<<','<<prefix<<"a"<<i;source<<")";
 const auto status=js_eval(r,source.str().c_str(),out);for(const auto&name:names)r->global->bindings.erase(name);collect(r);return status;
}
}
