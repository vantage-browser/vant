#pragma once
#include "Frontend.h"
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>
namespace jspp {
enum class Op { PushUndefined,PushNull,PushBoolean,PushNumber,PushString,Load,Declare,Store,EnterScope,LeaveScope,MakeFunction,Call,CallMethod,Construct,Return,Throw,BeginCompletion,ResumeCompletion,BreakCompletion,ContinueCompletion,MakeObject,MakeArray,DefineProperty,ArrayPush,GetProperty,SetProperty,UnaryPlus,Negate,Not,Add,Subtract,Multiply,Divide,Remainder,Equal,NotEqual,StrictEqual,StrictNotEqual,Less,LessEqual,Greater,GreaterEqual,InstanceOf,Pop,Duplicate,Jump,JumpIfFalse,JumpIfTrue };
struct Instruction { Op op;double number;bool boolean;std::string text;std::size_t target=0,line=0,column=0;Instruction(Op o=Op::PushUndefined,double n=0,bool b=false,std::string t={},std::size_t j=0):op(o),number(n),boolean(b),text(std::move(t)),target(j){} };
struct Bytecode;
struct FunctionPrototype { std::string name;std::vector<std::string>parameters;std::shared_ptr<Bytecode>code; };
struct HandlerRegion { std::size_t begin=0,end=0,target=0,scope_depth=0,stack_depth=0; };
struct CleanupRegion { std::size_t begin=0,end=0,target=0,scope_depth=0,stack_depth=0; };
struct Bytecode { std::vector<Instruction> instructions;std::vector<std::shared_ptr<FunctionPrototype>>functions;std::vector<HandlerRegion>handlers;std::vector<CleanupRegion>cleanups; };
bool compile(const Program&,Bytecode&,std::string&error);
std::string disassemble(const Bytecode&);
}
