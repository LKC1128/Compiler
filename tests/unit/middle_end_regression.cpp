#include "frontend/ir_builder.hpp"
#include "frontend/parser.hpp"
#include "ir/ir_verifier.hpp"
#include "ir/ir_printer.hpp"
#include "ir/clone.hpp"
#include "passes/middle_end.hpp"
#include "passes/mem2reg.hpp"
#include "passes/cfg_simplify.hpp"
#include "passes/const_folding.hpp"
#include "passes/dce.hpp"
#include "analysis/cfg.hpp"
#include "analysis/loop.hpp"
#include "backend/location_assignment.hpp"
#include "backend/arm64_codegen.hpp"
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
using namespace sysy;
namespace {
int checks=0;
void check(bool ok,const char* message) {++checks;if(!ok)throw std::runtime_error(message);}
void valid(Module& m) {std::string error;IRVerifier v;check(v.verify(m,error),error.c_str());}
Function* raw_function(Module& m,Type* ret=nullptr,bool arg=false) {
    auto& c=m.type_context();return m.add_function(std::make_unique<Function>(c.get_function_type(ret?ret:c.get_i32_type(),arg?std::vector<Type*>{c.get_i32_type()}:std::vector<Type*>{}),"f",&m));
}
struct Source {
    Module m;
    explicit Source(const std::string& source) {
        Lexer lexer(source);Parser parser(lexer);auto ast=parser.parse_comp_unit();
        check(!parser.has_error(),parser.error_message().c_str());IRBuilder builder(m);check(builder.build(*ast),builder.error_message().c_str());valid(m);
    }
    void mir() {PassManager p;LowerHIRPass lower;p.run_pass(lower,m);Mem2RegPass mem;p.run_pass(mem,m);CanonicalizePass can;p.run_pass(can,m);CFGSimplifyPass cfg;p.run_pass(cfg,m);valid(m);}
};
size_t count(const Module& m,Opcode op) {size_t n=0;for(auto& f:m.functions())for(auto& b:f->blocks())for(auto& i:b->instructions())n+=i->opcode()==op;return n;}
void poison_and_integer() {
    struct Case {Opcode op;int32_t a,b;unsigned flags;bool poison;int32_t value;};
    std::vector<Case> cases{
        {Opcode::Add,INT32_MAX,1,0,false,INT32_MIN},
        {Opcode::Add,INT32_MAX,1,UB_NSW,true,0},
        {Opcode::Add,-1,1,UB_NUW,true,0},
        {Opcode::Sub,0,1,UB_NUW,true,0},
        {Opcode::Mul,INT32_MAX,2,UB_NSW,true,0},
        {Opcode::SDiv,7,2,UB_EXACT,true,0},
        {Opcode::SDiv,8,2,UB_EXACT,false,4},
        {Opcode::SDiv,1,0,0,true,0},
        {Opcode::Shl,1,32,0,true,0},
        {Opcode::Shl,1,31,UB_NSW,true,0},
        {Opcode::LShr,-1,1,0,false,INT32_MAX},
        {Opcode::LShr,7,1,UB_EXACT,true,0},
        {Opcode::AShr,-8,2,UB_EXACT,false,-2},
        {Opcode::And,0x7f,0x33,0,false,0x33},
        {Opcode::Or,0x70,0x0f,0,false,0x7f},
        {Opcode::Xor,-1,0x55,0,false,-86}
    };
    for(auto tc:cases) {
        Module m;auto& c=m.type_context();auto* f=raw_function(m);auto* b=f->create_block(c);
        auto n=std::make_unique<BinaryInst>(tc.op,ConstantInt::get(c,tc.a),ConstantInt::get(c,tc.b));n->set_ub_flags(tc.flags);
        auto* value=b->append_instruction(std::move(n));auto* ret=static_cast<ReturnInst*>(b->append_instruction(std::make_unique<ReturnInst>(value,c)));
        CFGAnalysis::compute_cfg(*f);PassManager pm;ConstFoldingPass fold;pm.run_pass(fold,m);
        if(tc.poison)check(dynamic_cast<PoisonValue*>(ret->return_value())!=nullptr,"missing poison");
        else {auto* ci=dynamic_cast<ConstantInt*>(ret->return_value());check(ci && ci->value()==tc.value,"integer folding mismatch");}
        valid(m);
    }
    for(Opcode op:{Opcode::SExt,Opcode::ZExt,Opcode::Trunc}) {
        Module m;auto& c=m.type_context();Type* result=op==Opcode::Trunc?static_cast<Type*>(c.get_i1_type()):c.get_i32_type();
        auto* f=raw_function(m,result);auto* b=f->create_block(c);
        Value* input=op==Opcode::Trunc?ConstantInt::get(c,258):ConstantInt::get_bool(c,true);
        auto* cast=b->append_instruction(std::make_unique<CastInst>(op,input,result));
        auto* ret=static_cast<ReturnInst*>(b->append_instruction(std::make_unique<ReturnInst>(cast,c)));
        CFGAnalysis::compute_cfg(*f);ConstFoldingPass fold;PassManager pm;pm.run_pass(fold,m);
        auto* ci=dynamic_cast<ConstantInt*>(ret->return_value());check(ci && ci->value()==(op==Opcode::SExt?-1:op==Opcode::ZExt?1:0),"cast signedness mismatch");
    }
    Module m;auto& c=m.type_context();auto* f=raw_function(m);auto* b=f->create_block(c);
    auto bad=std::make_unique<BinaryInst>(Opcode::And,ConstantInt::get(c,1),ConstantInt::get(c,2));bad->set_ub_flags(UB_NSW);
    auto* n=b->append_instruction(std::move(bad));b->append_instruction(std::make_unique<ReturnInst>(n,c));CFGAnalysis::compute_cfg(*f);
    std::string error;IRVerifier verifier;check(!verifier.verify(m,error),"illegal flags accepted");
}
void canonical_and_contract() {
    {
        Module m;auto& c=m.type_context();auto* f=raw_function(m);
        auto* e=f->create_block(c);auto* yes=f->create_block(c);auto* no=f->create_block(c);
        e->append_instruction(std::make_unique<BranchInst>(ConstantInt::get_bool(c,true),yes,no,c));
        yes->append_instruction(std::make_unique<ReturnInst>(ConstantInt::get(c,3),c));
        no->append_instruction(std::make_unique<ReturnInst>(PoisonValue::get(c.get_i32_type()),c));
        CFGAnalysis::compute_cfg(*f);CanonicalizePass can;PassManager pm;pm.run_pass(can,m);valid(m);
        auto* br=dynamic_cast<BranchInst*>(e->get_terminator());check(br && !br->is_conditional() && br->target()==yes,"constant branch corrupted");
    }
    for(bool nsw:{false,true}) {
        Module m;auto& c=m.type_context();auto* f=raw_function(m,c.get_i1_type(),true);auto* b=f->create_block(c);
        auto add=std::make_unique<BinaryInst>(Opcode::Add,ConstantInt::get(c,1),f->get_arg(0));add->set_ub_flags(nsw?UB_NSW:0);
        auto* a=b->append_instruction(std::move(add));auto* cmp=b->append_instruction(std::make_unique<ICmpInst>(ICmpPred::SGT,a,f->get_arg(0),c));
        auto* ret=static_cast<ReturnInst*>(b->append_instruction(std::make_unique<ReturnInst>(cmp,c)));CFGAnalysis::compute_cfg(*f);
        auto before=canonical_rank(m);CanonicalizePass can;PassManager pm;check(pm.run_pass(can,m),"canonical rule not applied");
        check(canonical_rank(m)<before,"rank not strictly decreasing");
        check((dynamic_cast<ConstantInt*>(ret->return_value())!=nullptr)==nsw,"overflow contract ignored");
        check(!pm.run_pass(can,m),"canonical form not fixed point");
    }
    Source s("int f(int x){return x-3;}int main(){return f(getint());}");s.mir();
    CanonicalizePass can;PassManager pm;pm.run_pass(can,s.m);check(count(s.m,Opcode::Sub)==1,"x-C rewritten");
}
void gvn_dominance_and_memory() {
    Source siblings("int f(int x,int b){if(b)return x+7;else return x+7;}int main(){return f(getint(),getint());}");siblings.mir();
    GVNPass gvn;PassManager pm;pm.run_pass(gvn,siblings.m);check(count(siblings.m,Opcode::Add)==2,"GVN crossed sibling dominance");
    Source dominated("int f(int x,int b){int a=x+7;if(b)return a;else return x+7;}int main(){return f(getint(),getint());}");dominated.mir();
    check(pm.run_pass(gvn,dominated.m),"GVN failed dominating reuse");check(count(dominated.m,Opcode::Add)==1,"GVN did not remove expression");
    Source memory("int a[1];int touch(){a[0]=9;return 1;}int main(){int x=a[0];touch();return x+a[0];}");memory.mir();
    auto loads=count(memory.m,Opcode::Load),calls=count(memory.m,Opcode::Call);pm.run_pass(gvn,memory.m);
    check(count(memory.m,Opcode::Load)==loads && count(memory.m,Opcode::Call)==calls,"GVN removed memory side effects");
}
void inline_and_loops() {
    Source source("int f(int x){if(x<0)return -x;return x+3;}int main(){int a=getint();return f(a)+f(a-1);}");source.mir();
    auto calls=count(source.m,Opcode::Call);PassManager pm;InlinePass inl;check(pm.run_pass(inl,source.m),"inline failed");check(count(source.m,Opcode::Call)==calls-2,"inline return CFG lost");valid(source.m);
    Source recursive("int f(int x){if(x==0)return 1;return f(x-1)+1;}int main(){return f(3);}");recursive.mir();
    auto recursive_calls=count(recursive.m,Opcode::Call);pm.run_pass(inl,recursive.m);check(count(recursive.m,Opcode::Call)==recursive_calls,"recursive function expanded");
    Source zero("int a[1];int f(int n,int x){int i=0;int s=0;while(i<n){s=s+7/x+a[0]+x*3;i=i+1;}return s;}int main(){return f(0,0);}");zero.mir();
    LICMPass licm;pm.run_pass(licm,zero.m);auto* f=zero.m.get_function("f");auto loops=find_natural_loops(*f);check(loops.size()==1,"natural loop missing");
    bool div_inside=false,load_inside=false,mul_outside=false;
    for(auto& b:f->blocks())for(auto& i:b->instructions()) {bool inside=loops[0].blocks.count(b.get());
        div_inside |= inside && i->opcode()==Opcode::SDiv;load_inside |= inside && i->is_load();mul_outside |= !inside && i->opcode()==Opcode::Mul;
    }
    check(div_inside && load_inside && mul_outside,"LICM speculated unsafe memory/division or missed invariant");
    Source small("int main(){int i=0;int a=2;int b=3;while(i<3){int t=a;a=b;b=t+a;i=i+1;}return a+b;}");small.mir();
    UnrollPass unroll;check(pm.run_pass(unroll,small.m),"small loop not unrolled");check(find_natural_loops(*small.m.get_function("main")).empty(),"unroll left backedge");valid(small.m);
    Source induction("int f(int n){int i=0;int s=0;while(i<n){s=s+i*7;i=i+1;}return s;}int main(){return f(getint());}");induction.mir();
    InductionPass iv;check(pm.run_pass(iv,induction.m),"induction recurrence not formed");valid(induction.m);
    Source shift("int f(int x){return x*8;}int main(){return f(getint());}");shift.mir();StrengthReducePass sr;
    check(pm.run_pass(sr,shift.m) && count(shift.m,Opcode::Shl)==1,"power-of-two multiply not reduced");
}
void representation_and_allocation() {
    Source s("int f(int x){return x+1;}int main(){return f(getint());}");
    check(!s.m.get_function("main")->regions().empty(),"source regions missing");
    PassManager pm;GVNPass gvn;bool rejected=false;try{pm.run_pass(gvn,s.m);}catch(const std::exception&){rejected=true;}
    check(rejected,"MIR pass accepted HIR");s.mir();check(s.m.get_function("main")->regions().empty(),"HIR lowering kept region ownership");
    LowerLIRPass lower;pm.run_pass(lower,s.m);valid(s.m);
    for(auto& f:s.m.functions()) if(!f->is_declaration()) {
        check(f->level()==IRLevel::LIR && (f->state_flags()&SF_REGALLOC) && !(f->state_flags()&SF_SSA),"LIR state incorrect");
        for(auto& b:f->blocks())for(auto& i:b->instructions())check(!i->is_phi(),"LIR still has phi");
    }
    std::ostringstream text;IRPrinter(text).print(s.m);check(text.str().find("level=LIR")!=std::string::npos && text.str().find("-> x")!=std::string::npos,"LIR dump lacks locations");
    std::ostringstream assembly;ARM64CodeGen(assembly).generate(s.m);check(assembly.str().find("str x19")!=std::string::npos && assembly.str().find("ldr x19")!=std::string::npos,"callee save not emitted");
    Source bisect("int main(){int x=getint();return 3+x;}");PassManager skip(0);LowerHIRPass hir;skip.run_pass(hir,bisect.m);
    Mem2RegPass mem;check(!skip.run_pass(mem,bisect.m),"bisect zero ran optimization");skip.run_pass(lower,bisect.m);valid(bisect.m);
    Source pressure("int f(int a,int b){return a*b+a+b;}int main(){return f(getint(),getint());}");pressure.mir();pm.run_pass(lower,pressure.m);
    auto* fn=pressure.m.get_function("f");auto& locations=fn->machine_layout().values;
    locations[fn->get_arg(1)].reg=locations[fn->get_arg(0)].reg;
    std::string error;IRVerifier verifier;check(!verifier.verify(pressure.m,error),"interfering allocation accepted");
}
}
int main() {
    try {poison_and_integer();canonical_and_contract();gvn_dominance_and_memory();inline_and_loops();representation_and_allocation();}
    catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<"\n";return 1;}
    std::cout<<"PASS "<<checks<<" middle-end checks\n";return 0;
}
