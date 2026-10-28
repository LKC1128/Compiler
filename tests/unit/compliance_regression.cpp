// OpenAI Codex generated audit regressions; reviewed through baseline/fixed runs.
#include "frontend/ir_builder.hpp"
#include "frontend/parser.hpp"
#include "ir/ir_verifier.hpp"
#include "ir/ir_printer.hpp"
#include "passes/middle_end.hpp"
#include "passes/phi_simplify.hpp"
#include "passes/const_folding.hpp"
#include "analysis/cfg.hpp"
#include "backend/arm64_codegen.hpp"
#include <iostream>
#include <stdexcept>
using namespace sysy;
namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
void valid(Module& m) {std::string error;IRVerifier v;check(v.verify(m,error),error.c_str());}
Function* function(Module& m,Type* result=nullptr,std::vector<Type*> args={}) {
    auto& c=m.type_context();return m.add_function(std::make_unique<Function>(c.get_function_type(result?result:c.get_i32_type(),args),"main",&m));
}
void dead_semantics() {
    for(const std::string source:{"int main(){return 0; missing=1;}","int main(){return 0; break;}","int main(){return 0; int x; int x;}"}) {
        Module m;Lexer l(source);Parser p(l);auto ast=p.parse_comp_unit();check(!p.has_error(),"syntax rejected");
        IRBuilder b(m);check(!b.build(*ast),"invalid dead statement accepted");
        check(b.error_message().find("line 0")==std::string::npos,"missing source location");
    }
}
void duplicate_edge(bool dump=false) {
    Module m;auto& c=m.type_context();auto* f=function(m,nullptr,{c.get_i1_type()});
    auto* e=f->create_block(c);auto* h=f->create_block(c);auto* b=f->create_block(c);auto* x=f->create_block(c);
    e->append_instruction(std::make_unique<BranchInst>(f->get_arg(0),h,h,c));
    auto* p=static_cast<PhiInst*>(h->append_instruction(std::make_unique<PhiInst>(c.get_i32_type())));
    auto* cmp=h->append_instruction(std::make_unique<ICmpInst>(ICmpPred::SLT,p,ConstantInt::get(c,3),c));
    h->append_instruction(std::make_unique<BranchInst>(cmp,b,x,c));
    auto* next=b->append_instruction(std::make_unique<BinaryInst>(Opcode::Add,p,ConstantInt::get(c,1)));
    b->append_instruction(std::make_unique<BranchInst>(h,c));p->add_incoming(ConstantInt::get(c,0),e);p->add_incoming(next,b);
    x->append_instruction(std::make_unique<ReturnInst>(p,c));CFGAnalysis::compute_cfg(*f);valid(m);
    if(dump) {IRPrinter(std::cout).print(m);return;}
    PassManager pm;UnrollPass unroll;check(pm.run_pass(unroll,m),"eligible loop not unrolled");valid(m);
}
void self_phi() {
    Module m;auto& c=m.type_context();auto* f=function(m);auto* e=f->create_block(c);auto* dead=f->create_block(c);
    e->append_instruction(std::make_unique<ReturnInst>(ConstantInt::get(c,0),c));
    auto* p=static_cast<PhiInst*>(dead->append_instruction(std::make_unique<PhiInst>(c.get_i32_type())));p->add_incoming(p,dead);
    dead->append_instruction(std::make_unique<BinaryInst>(Opcode::Add,p,ConstantInt::get(c,1)));
    dead->append_instruction(std::make_unique<BranchInst>(dead,c));CFGAnalysis::compute_cfg(*f);valid(m);
    PassManager pm;PhiSimplifyPass pass;pm.run_pass(pass,m);valid(m);
}
void comparison_widths() {
    struct Case {unsigned bits;int32_t a,b;ICmpPred pred;bool result;};
    const Case cases[]={
        {1,1,0,ICmpPred::SLT,true},{1,1,0,ICmpPred::ULT,false},
        {1,0,1,ICmpPred::SGT,true},{1,0,1,ICmpPred::UGE,false},
        {8,128,127,ICmpPred::SLT,true},{8,128,127,ICmpPred::ULT,false},
        {8,255,0,ICmpPred::SLE,true},{8,255,0,ICmpPred::ULE,false},
        {8,128,128,ICmpPred::EQ,true},{8,255,127,ICmpPred::NE,true},
        {32,INT32_MIN,INT32_MAX,ICmpPred::SLT,true},
        {32,INT32_MIN,INT32_MAX,ICmpPred::ULT,false}
    };
    for(auto tc:cases) {
        Module m;auto& c=m.type_context();auto* f=function(m,c.get_i1_type());auto* b=f->create_block(c);
        auto constant=[&](int32_t x) {return tc.bits==1?ConstantInt::get_bool(c,x):tc.bits==8?ConstantInt::get_byte(c,uint8_t(x)):ConstantInt::get(c,x);};
        auto* cmp=b->append_instruction(std::make_unique<ICmpInst>(tc.pred,constant(tc.a),constant(tc.b),c));
        auto* ret=static_cast<ReturnInst*>(b->append_instruction(std::make_unique<ReturnInst>(cmp,c)));CFGAnalysis::compute_cfg(*f);valid(m);
        PassManager pm;ConstFoldingPass pass;pm.run_pass(pass,m);
        auto* result=dynamic_cast<ConstantInt*>(ret->return_value());check(result&&bool(result->value())==tc.result,"typed comparison mismatch");
    }
}
void narrow_compare(bool assembly) {
    Module m;auto& c=m.type_context();auto* f=function(m);auto* b=f->create_block(c);
    Value* left=ConstantInt::get_byte(c,255);Value* right=ConstantInt::get_byte(c,0);
    // The loaded i8 exercises backend comparison independently of folding.
    if(assembly) {
        auto* slot=b->append_instruction(std::make_unique<AllocaInst>(c.get_i8_type(),c));
        b->append_instruction(std::make_unique<StoreInst>(left,slot,c));
        left=b->append_instruction(std::make_unique<LoadInst>(slot));
    }
    auto* cmp=b->append_instruction(std::make_unique<ICmpInst>(ICmpPred::SLT,left,right,c));
    auto* result=b->append_instruction(std::make_unique<CastInst>(Opcode::ZExt,cmp,c.get_i32_type()));
    auto* ret=static_cast<ReturnInst*>(b->append_instruction(std::make_unique<ReturnInst>(result,c)));CFGAnalysis::compute_cfg(*f);valid(m);
    PassManager pm;
    if(assembly) {LowerLIRPass lower;pm.run_pass(lower,m);ARM64CodeGen(std::cout).generate(m);}
    else {ConstFoldingPass fold;pm.run_pass(fold,m);auto* value=dynamic_cast<ConstantInt*>(ret->return_value());check(value&&value->value()==1,"icmp slt i8 255,0 folded incorrectly");}
}
}
int main(int argc,char** argv) {
    std::string mode=argc>1?argv[1]:"all";
    try {
        if(mode=="edge-ir") {duplicate_edge(true);return 0;}
        if(mode=="all"||mode=="dead")dead_semantics();
        if(mode=="all"||mode=="edge")duplicate_edge();
        if(mode=="all"||mode=="phi")self_phi();
        if(mode=="all"||mode=="cmp") {narrow_compare(false);comparison_widths();}
        if(mode=="asm") {narrow_compare(true);return 0;}
    } catch(const std::exception& e) {std::cerr<<"FAIL "<<mode<<": "<<e.what()<<"\n";return 1;}
    std::cout<<"PASS "<<mode<<"\n";
}
