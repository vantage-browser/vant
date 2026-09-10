#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <vector>
namespace jspp {
struct Diagnostic { std::size_t offset=0,line=1,column=1;std::string message; };
enum class TokenKind { End,Identifier,Number,String,Keyword,Punct };
struct Token { TokenKind kind=TokenKind::End;std::string text;std::size_t offset=0,line=1,column=1; };
struct Statement;
enum class ExprKind { Number,String,Boolean,Null,Undefined,Identifier,This,Unary,Binary,Assignment,Conditional,Function,Call,New,Object,Array,Property };
struct Expr { ExprKind kind=ExprKind::Undefined;std::string text;double number=0;bool computed=false;std::unique_ptr<Expr>left,right,third;std::vector<std::unique_ptr<Expr>>arguments;std::vector<std::string>keys;std::vector<std::string>parameters;std::unique_ptr<Statement>function_body; };
enum class StatementKind { Declaration,Expression,Block,If,While,For,Break,Continue,Return,Throw,Try,FunctionDeclaration,Empty };
struct Statement {
 StatementKind kind=StatementKind::Expression;bool constant=false;std::string name;
 std::size_t line=1,column=1;
 std::unique_ptr<Expr>expression,condition,update;
 std::unique_ptr<Statement>initializer,then_branch,else_branch,try_branch,catch_branch,finally_branch;
 std::string catch_name;
 std::vector<Statement>body;
};
struct Program { std::vector<Statement> statements; };
class Lexer { public:explicit Lexer(std::string source):source_(std::move(source)){}bool lex(std::vector<Token>&,Diagnostic&);private:std::string source_; };
class Parser {
 public:Parser(const std::vector<Token>&t,Diagnostic&d):tokens_(t),diagnostic_(d){}bool parse(Program&);
 private:const std::vector<Token>&tokens_;Diagnostic&diagnostic_;std::size_t at_=0;
 std::unique_ptr<Expr>expression(),assignment(),conditional(),binary(int=0),prefix(),function_expression();
 bool statement(Statement&),declaration(Statement&,bool),parenthesized(std::unique_ptr<Expr>&),consume(const std::string&,const std::string&),fail(const Token&,const std::string&);
};
bool parse_source(const std::string&,Program&,Diagnostic&);
std::string format_diagnostic(const Diagnostic&);
}
