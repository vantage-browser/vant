#include "Frontend.h"
#include <cctype>
#include <cstdlib>
#include <sstream>
namespace jspp {
bool Lexer::lex(std::vector<Token>&out,Diagnostic&d){
 std::size_t i=0,line=1,col=1;auto advance=[&](char c){++i;if(c=='\n'){++line;col=1;}else ++col;};
 while(i<source_.size()){
  char c=source_[i];if(std::isspace(static_cast<unsigned char>(c))){advance(c);continue;}
  if(c=='/'&&i+1<source_.size()&&source_[i+1]=='/'){while(i<source_.size()&&source_[i]!='\n')advance(source_[i]);continue;}
  const auto start=i,sl=line,sc=col;
  if(std::isalpha(static_cast<unsigned char>(c))||c=='_'||c=='$'){
   while(i<source_.size()&&(std::isalnum(static_cast<unsigned char>(source_[i]))||source_[i]=='_'||source_[i]=='$'))advance(source_[i]);
   auto text=source_.substr(start,i-start);const bool keyword=text=="let"||text=="const"||text=="var"||text=="true"||text=="false"||text=="null"||text=="undefined"||text=="if"||text=="else"||text=="while"||text=="for"||text=="break"||text=="continue"||text=="function"||text=="return"||text=="throw"||text=="try"||text=="catch"||text=="finally"||text=="this"||text=="new"||text=="instanceof";
   out.push_back({keyword?TokenKind::Keyword:TokenKind::Identifier,text,start,sl,sc});continue;
  }
  if(std::isdigit(static_cast<unsigned char>(c))||(c=='.'&&i+1<source_.size()&&std::isdigit(static_cast<unsigned char>(source_[i+1])))){
   bool dot=false;while(i<source_.size()&&(std::isdigit(static_cast<unsigned char>(source_[i]))||(!dot&&source_[i]=='.'))){if(source_[i]=='.')dot=true;advance(source_[i]);}
   out.push_back({TokenKind::Number,source_.substr(start,i-start),start,sl,sc});continue;
  }
  if(c=='\''||c=='"'){
   const char quote=c;advance(c);std::string value;bool closed=false;
   while(i<source_.size()){c=source_[i];if(c==quote){advance(c);closed=true;break;}if(c=='\n'||c=='\r')break;if(c=='\\'){advance(c);if(i>=source_.size())break;c=source_[i];if(c=='n')value+='\n';else if(c=='t')value+='\t';else if(c==quote||c=='\\')value+=c;else{d={i,line,col,"unsupported string escape"};return false;}advance(c);}else{value+=c;advance(c);}}
   if(!closed){d={start,sl,sc,"unterminated string literal"};return false;}out.push_back({TokenKind::String,value,start,sl,sc});continue;
  }
  std::string op;if(i+2<source_.size()){auto x=source_.substr(i,3);if(x=="==="||x=="!==")op=x;}if(op.empty()&&i+1<source_.size()){auto x=source_.substr(i,2);if(x=="<="||x==">="||x=="=="||x=="!="||x=="&&"||x=="||"||x=="=>")op=x;}if(op.empty()&&std::string("+-*/%()=;<>{}[]!.?:,").find(c)!=std::string::npos)op=std::string(1,c);
  if(op.empty()){d={i,line,col,"unexpected character"};return false;}for(char x:op)advance(x);out.push_back({TokenKind::Punct,op,start,sl,sc});
 }
 out.push_back({TokenKind::End,"",i,line,col});return true;
}
static int precedence(const std::string&o){if(o=="||")return 1;if(o=="&&")return 2;if(o=="=="||o=="!="||o=="==="||o=="!==")return 3;if(o=="<"||o=="<="||o==">"||o==">="||o=="instanceof")return 4;if(o=="+"||o=="-")return 5;if(o=="*"||o=="/"||o=="%")return 6;return -1;}
bool Parser::fail(const Token&t,const std::string&m){diagnostic_={t.offset,t.line,t.column,m};return false;}
bool Parser::consume(const std::string&t,const std::string&m){if(tokens_[at_].text!=t)return fail(tokens_[at_],m);++at_;return true;}
std::unique_ptr<Expr>Parser::function_expression(){
 ++at_;auto e=std::make_unique<Expr>();e->kind=ExprKind::Function;
 if(tokens_[at_].kind==TokenKind::Identifier)e->text=tokens_[at_++].text;
 if(!consume("(","expected '(' after function"))return nullptr;
 if(tokens_[at_].text!=")")for(;;){if(tokens_[at_].kind!=TokenKind::Identifier){fail(tokens_[at_],"expected parameter name");return nullptr;}e->parameters.push_back(tokens_[at_++].text);if(tokens_[at_].text!=",")break;++at_;}
 if(!consume(")","expected ')' after parameters"))return nullptr;
 e->function_body=std::make_unique<Statement>();if(tokens_[at_].text!="{"){fail(tokens_[at_],"expected function body");return nullptr;}if(!statement(*e->function_body))return nullptr;return e;
}
std::unique_ptr<Expr>Parser::prefix(){
 const auto&t=tokens_[at_];if(t.text=="function")return function_expression();
 if(t.text=="{"){++at_;auto e=std::make_unique<Expr>();e->kind=ExprKind::Object;if(tokens_[at_].text!="}")for(;;){if(tokens_[at_].kind!=TokenKind::Identifier&&tokens_[at_].kind!=TokenKind::String){fail(tokens_[at_],"expected object property name");return nullptr;}auto key=tokens_[at_++].text;e->keys.push_back(key);if(tokens_[at_].text==":"){++at_;auto value=assignment();if(!value)return nullptr;e->arguments.push_back(std::move(value));}else if(tokens_[at_].text=="("){auto value=std::make_unique<Expr>();value->kind=ExprKind::Function;++at_;if(tokens_[at_].text!=")")for(;;){if(tokens_[at_].kind!=TokenKind::Identifier){fail(tokens_[at_],"expected parameter name");return nullptr;}value->parameters.push_back(tokens_[at_++].text);if(tokens_[at_].text!=",")break;++at_;}if(!consume(")","expected ')' after parameters"))return nullptr;value->function_body=std::make_unique<Statement>();if(tokens_[at_].text!="{"||!statement(*value->function_body))return nullptr;e->arguments.push_back(std::move(value));}else{auto value=std::make_unique<Expr>();value->kind=ExprKind::Identifier;value->text=key;e->arguments.push_back(std::move(value));}if(tokens_[at_].text!=",")break;++at_;}if(!consume("}","expected '}' after object literal"))return nullptr;return e;}
 if(t.text=="["){++at_;auto e=std::make_unique<Expr>();e->kind=ExprKind::Array;while(tokens_[at_].text!="]"){if(tokens_[at_].text==","){auto hole=std::make_unique<Expr>();hole->kind=ExprKind::Undefined;e->arguments.push_back(std::move(hole));++at_;continue;}auto value=assignment();if(!value)return nullptr;e->arguments.push_back(std::move(value));if(tokens_[at_].text!=",")break;++at_;}if(!consume("]","expected ']' after array literal"))return nullptr;return e;}
 if(t.kind==TokenKind::Number){++at_;auto e=std::make_unique<Expr>();e->kind=ExprKind::Number;e->number=std::strtod(t.text.c_str(),nullptr);return e;}
 if(t.kind==TokenKind::String){++at_;auto e=std::make_unique<Expr>();e->kind=ExprKind::String;e->text=t.text;return e;}
 if(t.kind==TokenKind::Identifier){++at_;auto e=std::make_unique<Expr>();e->kind=ExprKind::Identifier;e->text=t.text;return e;}
 if(t.text=="this"){++at_;auto e=std::make_unique<Expr>();e->kind=ExprKind::This;return e;}
 if(t.text=="new"){++at_;if(tokens_[at_].kind!=TokenKind::Identifier){fail(tokens_[at_],"expected constructor name");return nullptr;}auto e=std::make_unique<Expr>();e->kind=ExprKind::New;e->left=std::make_unique<Expr>();e->left->kind=ExprKind::Identifier;e->left->text=tokens_[at_++].text;if(!consume("(","expected '(' after constructor"))return nullptr;if(tokens_[at_].text!=")")for(;;){auto arg=expression();if(!arg)return nullptr;e->arguments.push_back(std::move(arg));if(tokens_[at_].text!=",")break;++at_;}if(!consume(")","expected ')' after constructor arguments"))return nullptr;return e;}
 if(t.kind==TokenKind::Keyword&&(t.text=="true"||t.text=="false"||t.text=="null"||t.text=="undefined")){++at_;auto e=std::make_unique<Expr>();e->kind=t.text=="true"||t.text=="false"?ExprKind::Boolean:t.text=="null"?ExprKind::Null:ExprKind::Undefined;e->text=t.text;return e;}
 if(t.text=="+"||t.text=="-"||t.text=="!"){++at_;auto e=std::make_unique<Expr>();e->kind=ExprKind::Unary;e->text=t.text;e->right=prefix();if(!e->right){fail(tokens_[at_],"expected expression after unary operator");return nullptr;}return e;}
 if(t.text=="("){++at_;auto e=expression();if(!e||!consume(")","expected ')'"))return nullptr;return e;}fail(t,"expected expression");return nullptr;
}
std::unique_ptr<Expr>Parser::binary(int min){
 auto left=prefix();if(!left)return nullptr;
 for(;;){if(tokens_[at_].text=="("){++at_;auto call=std::make_unique<Expr>();call->kind=ExprKind::Call;call->left=std::move(left);if(tokens_[at_].text!=")")for(;;){auto arg=expression();if(!arg)return nullptr;call->arguments.push_back(std::move(arg));if(tokens_[at_].text!=",")break;++at_;}if(!consume(")","expected ')' after arguments"))return nullptr;left=std::move(call);continue;}if(tokens_[at_].text=="."){++at_;if(tokens_[at_].kind!=TokenKind::Identifier){fail(tokens_[at_],"expected property name");return nullptr;}auto member=std::make_unique<Expr>();member->kind=ExprKind::Property;member->left=std::move(left);member->text=tokens_[at_++].text;left=std::move(member);continue;}if(tokens_[at_].text=="["){++at_;auto key=expression();if(!key||!consume("]","expected ']' after property key"))return nullptr;auto member=std::make_unique<Expr>();member->kind=ExprKind::Property;member->computed=true;member->left=std::move(left);member->right=std::move(key);left=std::move(member);continue;}break;}
 while(at_<tokens_.size()){auto op=tokens_[at_].text;int p=precedence(op);if(p<min)break;++at_;auto right=binary(p+1);if(!right)return nullptr;auto e=std::make_unique<Expr>();e->kind=ExprKind::Binary;e->text=op;e->left=std::move(left);e->right=std::move(right);left=std::move(e);}return left;
}
std::unique_ptr<Expr>Parser::conditional(){auto c=binary();if(!c||tokens_[at_].text!="?")return c;++at_;auto yes=assignment();if(!yes||!consume(":","expected ':' in conditional expression"))return nullptr;auto no=assignment();if(!no)return nullptr;auto e=std::make_unique<Expr>();e->kind=ExprKind::Conditional;e->left=std::move(c);e->right=std::move(yes);e->third=std::move(no);return e;}
std::unique_ptr<Expr>Parser::assignment(){
 std::vector<std::string>params;std::size_t after=at_;bool arrow=false;
 if(tokens_[at_].kind==TokenKind::Identifier&&tokens_[at_+1].text=="=>"){params.push_back(tokens_[at_].text);after=at_+2;arrow=true;}
 else if(tokens_[at_].text=="("){std::size_t scan=at_+1;bool valid=true;if(tokens_[scan].text!=")")for(;;){if(tokens_[scan].kind!=TokenKind::Identifier){valid=false;break;}params.push_back(tokens_[scan++].text);if(tokens_[scan].text!=",")break;++scan;}if(valid&&tokens_[scan].text==")"&&tokens_[scan+1].text=="=>"){after=scan+2;arrow=true;}else params.clear();}
 if(arrow){at_=after;auto e=std::make_unique<Expr>();e->kind=ExprKind::Function;e->parameters=std::move(params);e->function_body=std::make_unique<Statement>();e->function_body->kind=StatementKind::Block;if(tokens_[at_].text=="{"){if(!statement(*e->function_body))return nullptr;}else{Statement result;result.kind=StatementKind::Return;result.expression=assignment();if(!result.expression)return nullptr;e->function_body->body.push_back(std::move(result));}return e;}
 auto left=conditional();if(!left||tokens_[at_].text!="=")return left;if(left->kind!=ExprKind::Identifier&&left->kind!=ExprKind::Property){fail(tokens_[at_],"invalid assignment target");return nullptr;}++at_;auto right=assignment();if(!right)return nullptr;auto e=std::make_unique<Expr>();e->kind=ExprKind::Assignment;if(left->kind==ExprKind::Identifier)e->text=left->text;else e->left=std::move(left);e->right=std::move(right);return e;
}
std::unique_ptr<Expr>Parser::expression(){return assignment();}
bool Parser::parenthesized(std::unique_ptr<Expr>&e){return consume("(","expected '('")&&(e=expression())&&consume(")","expected ')'");}
bool Parser::declaration(Statement&s,bool semi){s.kind=StatementKind::Declaration;s.constant=tokens_[at_].text=="const";++at_;if(tokens_[at_].kind!=TokenKind::Identifier)return fail(tokens_[at_],"expected binding name");s.name=tokens_[at_++].text;if(!consume("=","declarations require an initializer"))return false;s.expression=expression();return s.expression&&(!semi||consume(";","expected ';' after declaration"));}
bool Parser::statement(Statement&s){
 s.line=tokens_[at_].line;s.column=tokens_[at_].column;
 const auto&t=tokens_[at_];if(t.text==";"){++at_;s.kind=StatementKind::Empty;return true;}
 if(t.text=="{"){++at_;s.kind=StatementKind::Block;while(tokens_[at_].kind!=TokenKind::End&&tokens_[at_].text!="}"){Statement x;if(!statement(x))return false;s.body.push_back(std::move(x));}return consume("}","expected '}'");}
 if(t.text=="function"){s.kind=StatementKind::FunctionDeclaration;s.expression=function_expression();if(!s.expression)return false;if(s.expression->text.empty())return fail(t,"function declaration requires a name");s.name=s.expression->text;return true;}
 if(t.text=="return"){++at_;s.kind=StatementKind::Return;if(tokens_[at_].text!=";"){s.expression=expression();if(!s.expression)return false;}return consume(";","expected ';' after return");}
 if(t.text=="throw"){++at_;s.kind=StatementKind::Throw;if(tokens_[at_].text==";")return fail(tokens_[at_],"throw requires an expression");s.expression=expression();return s.expression&&consume(";","expected ';' after throw");}
 if(t.text=="try"){++at_;s.kind=StatementKind::Try;if(tokens_[at_].text!="{")return fail(tokens_[at_],"expected block after try");s.try_branch=std::make_unique<Statement>();if(!statement(*s.try_branch))return false;if(tokens_[at_].text=="catch"){++at_;if(tokens_[at_].text=="("){++at_;if(tokens_[at_].kind!=TokenKind::Identifier)return fail(tokens_[at_],"expected catch binding");s.catch_name=tokens_[at_++].text;if(!consume(")","expected ')' after catch binding"))return false;}if(tokens_[at_].text!="{")return fail(tokens_[at_],"expected block after catch");s.catch_branch=std::make_unique<Statement>();if(!statement(*s.catch_branch))return false;}if(tokens_[at_].text=="finally"){++at_;if(tokens_[at_].text!="{")return fail(tokens_[at_],"expected block after finally");s.finally_branch=std::make_unique<Statement>();if(!statement(*s.finally_branch))return false;}if(!s.catch_branch&&!s.finally_branch)return fail(tokens_[at_],"expected catch or finally after try block");return true;}
 if(t.text=="if"){++at_;s.kind=StatementKind::If;if(!parenthesized(s.condition))return false;s.then_branch=std::make_unique<Statement>();if(!statement(*s.then_branch))return false;if(tokens_[at_].text=="else"){++at_;s.else_branch=std::make_unique<Statement>();if(!statement(*s.else_branch))return false;}return true;}
 if(t.text=="while"){++at_;s.kind=StatementKind::While;if(!parenthesized(s.condition))return false;s.then_branch=std::make_unique<Statement>();return statement(*s.then_branch);}
 if(t.text=="for"){++at_;s.kind=StatementKind::For;if(!consume("(","expected '('") )return false;if(tokens_[at_].text!=";"){s.initializer=std::make_unique<Statement>();if(tokens_[at_].text=="let"||tokens_[at_].text=="const"||tokens_[at_].text=="var"){if(!declaration(*s.initializer,false))return false;}else{s.initializer->expression=expression();if(!s.initializer->expression)return false;}}if(!consume(";","expected ';' in for statement"))return false;if(tokens_[at_].text!=";"){s.condition=expression();if(!s.condition)return false;}if(!consume(";","expected ';' in for statement"))return false;if(tokens_[at_].text!=")"){s.update=expression();if(!s.update)return false;}if(!consume(")","expected ')'"))return false;s.then_branch=std::make_unique<Statement>();return statement(*s.then_branch);}
 if(t.text=="break"||t.text=="continue"){s.kind=t.text=="break"?StatementKind::Break:StatementKind::Continue;++at_;return consume(";","expected ';' after loop control");}
 if(t.kind==TokenKind::Keyword&&(t.text=="let"||t.text=="const"||t.text=="var"))return declaration(s,true);
 s.kind=StatementKind::Expression;s.expression=expression();if(!s.expression)return false;if(tokens_[at_].text==";")++at_;else if(tokens_[at_].kind!=TokenKind::End&&tokens_[at_].text!="}")return fail(tokens_[at_],"expected ';' or end of input");return true;
}
bool Parser::parse(Program&p){while(tokens_[at_].kind!=TokenKind::End){Statement s;if(!statement(s))return false;p.statements.push_back(std::move(s));}return true;}
bool parse_source(const std::string&s,Program&p,Diagnostic&d){std::vector<Token>t;Lexer l(s);return l.lex(t,d)&&Parser(t,d).parse(p);}
std::string format_diagnostic(const Diagnostic&d){std::ostringstream o;o<<d.line<<":"<<d.column<<": "<<d.message;return o.str();}
}
