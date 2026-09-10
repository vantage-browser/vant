#include "Intrinsics.h"
#include "Property.h"
#include "Error.h"
#include "Conversion.h"
#include <algorithm>
#include <cmath>

namespace jspp {
namespace {
js_value string_value(std::string text){js_value value;value.kind=JS_VALUE_STRING;value.string=std::move(text);return value;}
js_value number_value(double number){js_value value;value.kind=JS_VALUE_NUMBER;value.number=number;return value;}
js_value boolean_value(bool boolean){js_value value;value.kind=JS_VALUE_BOOLEAN;value.boolean=boolean;return value;}
js_value undefined_value(){return {};}
bool object_value(const js_value&value,std::shared_ptr<ObjectValue>&object){if(value.kind==JS_VALUE_OBJECT&&value.object){object=value.object;return true;}return false;}
bool own(const std::shared_ptr<ObjectValue>&object,const std::string&name,js_value&value){auto found=object->properties.find(name);if(found==object->properties.end())return false;value=found->second;return true;}
bool property_maps(const js_value&value,std::unordered_map<std::string,js_value>*&properties,std::unordered_map<std::string,PropertyAttributes>*&attributes){auto maps=own_property_maps(value);properties=maps.values;attributes=maps.attributes;return static_cast<bool>(maps);}
}

js_value make_native(Heap&heap,const std::string&name,std::size_t length,bool constructible,
                     NativeFunction callback,const std::shared_ptr<ObjectValue>&function_prototype,
                     const std::shared_ptr<ObjectValue>&instance_prototype){
    js_value value;value.kind=JS_VALUE_FUNCTION;value.function=heap.function();
    value.function->native=std::move(callback);value.function->name=name;
    value.function->length=length;value.function->constructible=constructible;
    value.function->object_prototype=function_prototype;
    value.function->instance_prototype=instance_prototype;
    value.function->properties["name"]=string_value(name);
    value.function->properties["length"]=number_value(static_cast<double>(length));
    return value;
}

IntrinsicSet create_intrinsics(Heap&heap){
    IntrinsicSet set;set.global=heap.environment();set.object_prototype=heap.object();
    set.function_prototype=heap.object();set.function_prototype->prototype=set.object_prototype;
    auto object=make_native(heap,"Object",1,true,
        [prototype=set.object_prototype](const std::vector<js_value>&args,const js_value&,bool,js_value&out,std::string&,Heap&heap,const NativeInvoke&){
            if(!args.empty()&&(args[0].kind==JS_VALUE_OBJECT||args[0].kind==JS_VALUE_ARRAY||args[0].kind==JS_VALUE_FUNCTION)){out=args[0];return true;}
            out.kind=JS_VALUE_OBJECT;out.object=heap.object();out.object->prototype=prototype;return true;
        },set.function_prototype,set.object_prototype);
    auto value_of=make_native(heap,"valueOf",0,false,
        [](const std::vector<js_value>&,const js_value&receiver,bool,js_value&out,std::string&,Heap&,const NativeInvoke&){out=receiver;return true;},
        set.function_prototype,set.object_prototype);
    auto object_to_string=make_native(heap,"toString",0,false,
        [](const std::vector<js_value>&,const js_value&,bool,js_value&out,std::string&,Heap&,const NativeInvoke&){out=string_value("[object Object]");return true;},
        set.function_prototype,set.object_prototype);
    set.object_prototype->properties["valueOf"]=value_of;
    set.object_prototype->properties["toString"]=object_to_string;

    object.function->properties["create"]=make_native(heap,"create",2,false,
        [](const std::vector<js_value>&args,const js_value&,bool,js_value&out,std::string&error,Heap&heap,const NativeInvoke&){
            if(args.empty()||(args[0].kind!=JS_VALUE_OBJECT&&args[0].kind!=JS_VALUE_NULL)){error="Object.create prototype must be an object or null";return false;}
            out.kind=JS_VALUE_OBJECT;out.object=heap.object();if(args[0].kind==JS_VALUE_OBJECT)out.object->prototype=args[0].object;return true;
        },set.function_prototype,set.object_prototype);
    object.function->properties["getPrototypeOf"]=make_native(heap,"getPrototypeOf",1,false,
        [](const std::vector<js_value>&args,const js_value&,bool,js_value&out,std::string&error,Heap&,const NativeInvoke&){
            std::shared_ptr<ObjectValue>value;if(args.empty()||!object_value(args[0],value)){error="Object.getPrototypeOf requires an object";return false;}
            if(value->prototype){out.kind=JS_VALUE_OBJECT;out.object=value->prototype;}else out.kind=JS_VALUE_NULL;return true;
        },set.function_prototype,set.object_prototype);
    object.function->properties["setPrototypeOf"]=make_native(heap,"setPrototypeOf",2,false,
        [](const std::vector<js_value>&args,const js_value&,bool,js_value&out,std::string&error,Heap&,const NativeInvoke&){
            std::shared_ptr<ObjectValue>value;if(args.size()<2||!object_value(args[0],value)||(args[1].kind!=JS_VALUE_OBJECT&&args[1].kind!=JS_VALUE_NULL)){error="Object.setPrototypeOf requires object and object-or-null prototype";return false;}
            auto prototype=args[1].kind==JS_VALUE_OBJECT?args[1].object:std::shared_ptr<ObjectValue>{};for(auto at=prototype;at;at=at->prototype)if(at==value){error="cyclic prototype value";return false;}value->prototype=prototype;out=args[0];return true;
        },set.function_prototype,set.object_prototype);
    object.function->properties["defineProperty"]=make_native(heap,"defineProperty",3,false,
        [](const std::vector<js_value>&args,const js_value&,bool,js_value&out,std::string&error,Heap&,const NativeInvoke&){
            std::shared_ptr<ObjectValue>descriptor;std::unordered_map<std::string,js_value>*properties=nullptr;std::unordered_map<std::string,PropertyAttributes>*attribute_map=nullptr;if(args.size()<3||!property_maps(args[0],properties,attribute_map)||!object_value(args[2],descriptor)){error="Object.defineProperty requires object, key and descriptor";return false;}std::string key;if(!to_property_key(args[1],key)){error="invalid property key";return false;}
            auto existing=attribute_map->find(key);if(existing!=attribute_map->end()&&!existing->second.configurable){error="cannot redefine non-configurable property '"+key+"'";return false;}
            js_value field;PropertyAttributes attributes;attributes.writable=false;attributes.enumerable=false;attributes.configurable=false;const bool has_value=own(descriptor,"value",field);if(has_value)(*properties)[key]=field;const bool has_get=own(descriptor,"get",field);if(has_get){if(field.kind!=JS_VALUE_UNDEFINED&&field.kind!=JS_VALUE_FUNCTION){error="property getter must be callable";return false;}attributes.getter=field;attributes.accessor=true;}const bool has_set=own(descriptor,"set",field);if(has_set){if(field.kind!=JS_VALUE_UNDEFINED&&field.kind!=JS_VALUE_FUNCTION){error="property setter must be callable";return false;}attributes.setter=field;attributes.accessor=true;}if(attributes.accessor&&has_value){error="invalid mixed data and accessor descriptor";return false;}if(attributes.accessor)(*properties)[key]={};if(!attributes.accessor&&!has_value)(*properties)[key]={};if(own(descriptor,"writable",field))attributes.writable=to_boolean(field);if(own(descriptor,"enumerable",field))attributes.enumerable=to_boolean(field);if(own(descriptor,"configurable",field))attributes.configurable=to_boolean(field);(*attribute_map)[key]=attributes;out=args[0];return true;
        },set.function_prototype,set.object_prototype);
    object.function->properties["getOwnPropertyDescriptor"]=make_native(heap,"getOwnPropertyDescriptor",2,false,
        [prototype=set.object_prototype](const std::vector<js_value>&args,const js_value&,bool,js_value&out,std::string&error,Heap&heap,const NativeInvoke&){std::unordered_map<std::string,js_value>*properties=nullptr;std::unordered_map<std::string,PropertyAttributes>*attributes=nullptr;if(args.size()<2||!property_maps(args[0],properties,attributes)){error="Object.getOwnPropertyDescriptor requires an object";return false;}std::string key;if(!to_property_key(args[1],key)){error="invalid property key";return false;}auto value=properties->find(key);auto attribute=attributes->find(key);if(value==properties->end()&&attribute==attributes->end()){out={};return true;}PropertyAttributes defaults;const auto&flags=attribute==attributes->end()?defaults:attribute->second;out.kind=JS_VALUE_OBJECT;out.object=heap.object();out.object->prototype=prototype;if(flags.accessor){out.object->properties["get"]=flags.getter;out.object->properties["set"]=flags.setter;}else{out.object->properties["value"]=value==properties->end()?js_value{}:value->second;out.object->properties["writable"]=boolean_value(flags.writable);}out.object->properties["enumerable"]=boolean_value(flags.enumerable);out.object->properties["configurable"]=boolean_value(flags.configurable);return true;},set.function_prototype,set.object_prototype);
    object.function->properties["keys"]=make_native(heap,"keys",1,false,
        [](const std::vector<js_value>&args,const js_value&,bool,js_value&out,std::string&error,Heap&heap,const NativeInvoke&){
            std::unordered_map<std::string,js_value>*properties=nullptr;std::unordered_map<std::string,PropertyAttributes>*attributes=nullptr;if(args.empty()||!property_maps(args[0],properties,attributes)){error="Object.keys requires an object";return false;}std::vector<std::string>keys;for(const auto&entry:*properties){auto flags=attributes->find(entry.first);if(flags==attributes->end()||flags->second.enumerable)keys.push_back(entry.first);}std::sort(keys.begin(),keys.end());out.kind=JS_VALUE_ARRAY;out.array=heap.array();for(auto&key:keys)out.array->elements.push_back(string_value(std::move(key)));return true;
        },set.function_prototype,set.object_prototype);

    auto function_call=make_native(heap,"call",1,false,
        [](const std::vector<js_value>&args,const js_value&receiver,bool,js_value&out,std::string&error,Heap&,const NativeInvoke&invoke){
            if(receiver.kind!=JS_VALUE_FUNCTION){error="Function.prototype.call receiver is not callable";return false;}
            const auto this_value=args.empty()?undefined_value():args[0];
            std::vector<js_value>forwarded;if(args.size()>1)forwarded.assign(args.begin()+1,args.end());
            return invoke(receiver,forwarded,this_value,false,out);
        },set.function_prototype,set.object_prototype);
    auto function_apply=make_native(heap,"apply",2,false,
        [](const std::vector<js_value>&args,const js_value&receiver,bool,js_value&out,std::string&error,Heap&,const NativeInvoke&invoke){
            if(receiver.kind!=JS_VALUE_FUNCTION){error="Function.prototype.apply receiver is not callable";return false;}
            const auto this_value=args.empty()?undefined_value():args[0];std::vector<js_value>forwarded;
            if(args.size()>1&&args[1].kind!=JS_VALUE_NULL&&args[1].kind!=JS_VALUE_UNDEFINED){if(args[1].kind!=JS_VALUE_ARRAY||!args[1].array){error="Function.prototype.apply arguments must be an array";return false;}forwarded=args[1].array->elements;}
            return invoke(receiver,forwarded,this_value,false,out);
        },set.function_prototype,set.object_prototype);
    set.function_prototype->properties["call"]=function_call;
    set.function_prototype->properties["apply"]=function_apply;

    auto function=make_native(heap,"Function",1,true,
        [function_prototype=set.function_prototype,object_prototype=set.object_prototype](const std::vector<js_value>&,const js_value&,bool,js_value&out,std::string&,Heap&heap,const NativeInvoke&){
            out=make_native(heap,"anonymous",0,true,[](const std::vector<js_value>&,const js_value&,bool,js_value&result,std::string&,Heap&,const NativeInvoke&){result={};return true;},function_prototype,object_prototype);return true;
        },set.function_prototype,set.function_prototype);

    auto array_prototype=heap.object();array_prototype->prototype=set.object_prototype;
    auto array=make_native(heap,"Array",1,true,
        [array_prototype](const std::vector<js_value>&args,const js_value&,bool,js_value&out,std::string&error,Heap&heap,const NativeInvoke&){out.kind=JS_VALUE_ARRAY;out.array=heap.array();out.array->prototype=array_prototype;if(args.size()==1&&args[0].kind==JS_VALUE_NUMBER){if(args[0].number<0||std::floor(args[0].number)!=args[0].number){error="invalid array length";return false;}out.array->elements.resize(static_cast<std::size_t>(args[0].number));}else out.array->elements=args;return true;},
        set.function_prototype,array_prototype);
    array.function->properties["isArray"]=make_native(heap,"isArray",1,false,
        [](const std::vector<js_value>&args,const js_value&,bool,js_value&out,std::string&,Heap&,const NativeInvoke&){out.kind=JS_VALUE_BOOLEAN;out.boolean=!args.empty()&&args[0].kind==JS_VALUE_ARRAY;return true;},set.function_prototype,set.object_prototype);
    array_prototype->properties["push"]=make_native(heap,"push",1,false,
        [](const std::vector<js_value>&args,const js_value&receiver,bool,js_value&out,std::string&error,Heap&,const NativeInvoke&){if(receiver.kind!=JS_VALUE_ARRAY){error="Array.prototype.push receiver is not an array";return false;}receiver.array->elements.insert(receiver.array->elements.end(),args.begin(),args.end());out=number_value(receiver.array->elements.size());return true;},set.function_prototype,set.object_prototype);
    array_prototype->properties["pop"]=make_native(heap,"pop",0,false,
        [](const std::vector<js_value>&,const js_value&receiver,bool,js_value&out,std::string&error,Heap&,const NativeInvoke&){if(receiver.kind!=JS_VALUE_ARRAY){error="Array.prototype.pop receiver is not an array";return false;}if(receiver.array->elements.empty()){out={};return true;}out=receiver.array->elements.back();receiver.array->elements.pop_back();return true;},set.function_prototype,set.object_prototype);
    array_prototype->properties["join"]=make_native(heap,"join",1,false,
        [](const std::vector<js_value>&args,const js_value&receiver,bool,js_value&out,std::string&error,Heap&,const NativeInvoke&){if(receiver.kind!=JS_VALUE_ARRAY){error="Array.prototype.join receiver is not an array";return false;}const auto separator=args.empty()?std::string(","):to_string(args[0]);std::string text;for(std::size_t i=0;i<receiver.array->elements.size();++i){if(i)text+=separator;const auto&value=receiver.array->elements[i];if(value.kind!=JS_VALUE_UNDEFINED&&value.kind!=JS_VALUE_NULL)text+=to_string(value);}out=string_value(std::move(text));return true;},set.function_prototype,set.object_prototype);
    array_prototype->properties["indexOf"]=make_native(heap,"indexOf",1,false,
        [](const std::vector<js_value>&args,const js_value&receiver,bool,js_value&out,std::string&error,Heap&,const NativeInvoke&){if(receiver.kind!=JS_VALUE_ARRAY){error="Array.prototype.indexOf receiver is not an array";return false;}std::size_t found=receiver.array->elements.size();if(!args.empty())for(std::size_t i=0;i<receiver.array->elements.size();++i)if(strict_equal(receiver.array->elements[i],args[0])){found=i;break;}out=number_value(found==receiver.array->elements.size()?-1.0:static_cast<double>(found));return true;},set.function_prototype,set.object_prototype);
    array_prototype->properties["slice"]=make_native(heap,"slice",2,false,
        [array_prototype](const std::vector<js_value>&args,const js_value&receiver,bool,js_value&out,std::string&error,Heap&heap,const NativeInvoke&){if(receiver.kind!=JS_VALUE_ARRAY){error="Array.prototype.slice receiver is not an array";return false;}const auto size=static_cast<double>(receiver.array->elements.size());auto index=[&](std::size_t n,double fallback){double value=fallback;if(n<args.size()&&!to_number(args[n],value))value=fallback;if(value<0)value=std::max(0.0,size+value);return static_cast<std::size_t>(std::min(size,std::max(0.0,std::floor(value))));};auto begin=index(0,0),end=index(1,size);if(end<begin)end=begin;out.kind=JS_VALUE_ARRAY;out.array=heap.array();out.array->prototype=array_prototype;out.array->elements.assign(receiver.array->elements.begin()+begin,receiver.array->elements.begin()+end);return true;},set.function_prototype,set.object_prototype);
    array_prototype->properties["map"]=make_native(heap,"map",1,false,
        [array_prototype](const std::vector<js_value>&args,const js_value&receiver,bool,js_value&out,std::string&error,Heap&heap,const NativeInvoke&invoke){if(receiver.kind!=JS_VALUE_ARRAY){error="Array.prototype.map receiver is not an array";return false;}if(args.empty()||args[0].kind!=JS_VALUE_FUNCTION){error="Array.prototype.map callback is not callable";return false;}out.kind=JS_VALUE_ARRAY;out.array=heap.array();out.array->prototype=array_prototype;for(std::size_t i=0;i<receiver.array->elements.size();++i){js_value mapped;if(!invoke(args[0],{receiver.array->elements[i],number_value(i),receiver},{},false,mapped))return false;out.array->elements.push_back(std::move(mapped));}return true;},set.function_prototype,set.object_prototype);

    auto string_prototype=heap.object();string_prototype->prototype=set.object_prototype;
    auto string=make_native(heap,"String",1,true,
        [](const std::vector<js_value>&args,const js_value&,bool,js_value&out,std::string&,Heap&,const NativeInvoke&){out=string_value(args.empty()?std::string{}:to_string(args[0]));return true;},set.function_prototype,string_prototype);
    string_prototype->properties["slice"]=make_native(heap,"slice",2,false,
        [](const std::vector<js_value>&args,const js_value&receiver,bool,js_value&out,std::string&error,Heap&,const NativeInvoke&){if(receiver.kind!=JS_VALUE_STRING){error="String.prototype.slice receiver is not a string";return false;}const auto size=static_cast<double>(receiver.string.size());auto index=[&](std::size_t n,double fallback){double value=fallback;if(n<args.size()&&!to_number(args[n],value))value=fallback;if(value<0)value=std::max(0.0,size+value);return static_cast<std::size_t>(std::min(size,std::max(0.0,std::floor(value))));};auto begin=index(0,0),end=index(1,size);if(end<begin)end=begin;out=string_value(receiver.string.substr(begin,end-begin));return true;},set.function_prototype,set.object_prototype);
    string_prototype->properties["includes"]=make_native(heap,"includes",1,false,
        [](const std::vector<js_value>&args,const js_value&receiver,bool,js_value&out,std::string&error,Heap&,const NativeInvoke&){if(receiver.kind!=JS_VALUE_STRING){error="String.prototype.includes receiver is not a string";return false;}out.kind=JS_VALUE_BOOLEAN;out.boolean=receiver.string.find(args.empty()?std::string("undefined"):to_string(args[0]))!=std::string::npos;return true;},set.function_prototype,set.object_prototype);
    string_prototype->properties["indexOf"]=make_native(heap,"indexOf",1,false,
        [](const std::vector<js_value>&args,const js_value&receiver,bool,js_value&out,std::string&error,Heap&,const NativeInvoke&){if(receiver.kind!=JS_VALUE_STRING){error="String.prototype.indexOf receiver is not a string";return false;}const auto found=receiver.string.find(args.empty()?std::string("undefined"):to_string(args[0]));out=number_value(found==std::string::npos?-1.0:static_cast<double>(found));return true;},set.function_prototype,set.object_prototype);

    auto make_error=[&](const std::string&name,const std::shared_ptr<ObjectValue>&prototype){
        return make_native(heap,name,1,true,[name,prototype](const std::vector<js_value>&args,const js_value&,bool,js_value&out,std::string&,Heap&heap,const NativeInvoke&){
            out=error_object(heap,name,args.empty()?std::string{}:to_string(args[0]),prototype);return true;
        },set.function_prototype,prototype);
    };
    auto error_prototype=heap.object();error_prototype->prototype=set.object_prototype;error_prototype->properties["name"]=string_value("Error");error_prototype->properties["message"]=string_value("");
    auto type_error_prototype=heap.object();type_error_prototype->prototype=error_prototype;type_error_prototype->properties["name"]=string_value("TypeError");
    auto range_error_prototype=heap.object();range_error_prototype->prototype=error_prototype;range_error_prototype->properties["name"]=string_value("RangeError");
    auto error=make_error("Error",error_prototype),type_error=make_error("TypeError",type_error_prototype),range_error=make_error("RangeError",range_error_prototype);

    for(auto entry:std::vector<std::pair<std::string,js_value>>{{"Object",object},{"Function",function},{"Array",array},{"String",string},{"Error",error},{"TypeError",type_error},{"RangeError",range_error}}){set.global->bindings[entry.first]={entry.second,true};set.roots.push_back(entry.second);}
    return set;
}
}
