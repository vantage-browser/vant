#pragma once
#include "VM.h"
#include <memory>
#include <vector>

namespace jspp {
struct IntrinsicSet {
    std::shared_ptr<Environment> global;
    std::vector<js_value> roots;
    std::shared_ptr<ObjectValue> object_prototype;
    std::shared_ptr<ObjectValue> function_prototype;
};

js_value make_native(Heap&,const std::string&,std::size_t,bool,NativeFunction,
                     const std::shared_ptr<ObjectValue>&,
                     const std::shared_ptr<ObjectValue>&);
IntrinsicSet create_intrinsics(Heap&);
}
