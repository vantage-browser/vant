#pragma once
#include "Runtime.h"
#include <cstddef>
#include <string>
namespace jspp {
bool to_boolean(const js_value&);
js_value to_primitive(const js_value&);
bool to_number(const js_value&,double&);
std::string to_string(const js_value&);
bool to_property_key(const js_value&,std::string&);
bool array_index(const js_value&,std::size_t&);
bool strict_equal(const js_value&,const js_value&);
bool abstract_equal(const js_value&,const js_value&);
bool relational_compare(const js_value&,const js_value&,int&);
}
