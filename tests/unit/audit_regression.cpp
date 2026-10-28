#include "frontend/ir_builder.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "ir/ir_verifier.hpp"
#include "ir/constant.hpp"
#include "ir/int_math.hpp"
#include "analysis/cfg.hpp"
#include "analysis/dominator.hpp"
#include "passes/const_folding.hpp"
#include <iostream>
#include <set>
#include <random>
#include <limits>
using namespace sysy;
namespace {
int failures=0;
void expect(bool ok,const std::string& label) { if(!ok) {++failures;std::cerr<<"FAIL: "<<label<<"\n";} }
Function* function(Module& m) {
    return m.add_function(std::make_unique<Function>(m.type_context().get_function_type(m.type_context().get_i32_type(),{}),"f",&m));
}
void verify_rejections() {
    for(int mode=0;mode<5;++mode) {
        Module m;auto& c=m.type_context();auto* f=function(m);
        auto* e=f->create_block(c);auto* l=f->create_block(c);auto* r=f->create_block(c);auto* j=f->create_block(c);
        e->append_instruction(std::make_unique<BranchInst>(ConstantInt::get_bool(c,true),l,r,c));
        auto* x=l->append_instruction(std::make_unique<BinaryInst>(Opcode::Add,ConstantInt::get(c,1),ConstantInt::get(c,2)));
        l->append_instruction(std::make_unique<BranchInst>(j,c));r->append_instruction(std::make_unique<BranchInst>(j,c));
        if(mode==0) j->append_instruction(std::make_unique<ReturnInst>(x,c));
        if(mode==1 || mode==2) {
            auto* p=static_cast<PhiInst*>(j->append_instruction(std::make_unique<PhiInst>(c.get_i32_type())));
            p->add_incoming(ConstantInt::get(c,1),l);
            if(mode==2) {p->add_incoming(ConstantInt::get(c,2),l);p->add_incoming(ConstantInt::get(c,3),r);}
            j->append_instruction(std::make_unique<ReturnInst>(p,c));
        }
        if(mode==3) {
            auto* bad=j->append_instruction(std::make_unique<BinaryInst>(Opcode::Add,ConstantInt::get(c,1),ConstantFloat::get(c,2)));
            j->append_instruction(std::make_unique<ReturnInst>(bad,c));
        }
        if(mode==4) j->append_instruction(std::make_unique<ReturnInst>(ConstantInt::get(c,1),c));
        if(mode!=4) CFGAnalysis::compute_cfg(*f);
        std::string error;expect(!IRVerifier().verify(m,error) && !error.empty(),"invalid IR mode "+std::to_string(mode));
    }
}
void same_target_phi() {
    Module m;auto& c=m.type_context();auto* f=function(m);auto* e=f->create_block(c);auto* j=f->create_block(c);
    e->append_instruction(std::make_unique<BranchInst>(ConstantInt::get_bool(c,true),j,j,c));
    auto* p=static_cast<PhiInst*>(j->append_instruction(std::make_unique<PhiInst>(c.get_i32_type())));
    p->add_incoming(ConstantInt::get(c,7),e);j->append_instruction(std::make_unique<ReturnInst>(p,c));
    CFGAnalysis::compute_cfg(*f);std::string error;
    expect(IRVerifier().verify(m,error),"same-target initial phi");
    ConstFoldingPass().run_on_function(*f);
    expect(p->num_incoming()==1 && p->incoming_block(0)==e,"same-target surviving incoming");
    expect(IRVerifier().verify(m,error),"same-target folded phi");
}
void lifetime_contexts_and_uses() {
    TypeContext c1,c2;
    auto* a=ConstantInt::get(c1,12345678);auto* b=ConstantInt::get(c2,12345678);
    expect(a!=b && b->type()==c2.get_i32_type(),"context-scoped integers");
    expect(ConstantFloat::get(c1,1)!=ConstantFloat::get(c2,1),"context-scoped floats");
    for(int n=0;n<10;++n) {
        Module m;Lexer lexer("int f(int x){return x+1;}int main(){int x=1;return f(x);}");Parser parser(lexer);
        auto ast=parser.parse_comp_unit();IRBuilder builder(m);expect(builder.build(*ast),"frontend/lifetime");
        std::string error;expect(IRVerifier().verify(m,error),"frontend valid IR");
        auto* one=ConstantInt::get(m.type_context(),1);
        auto* edge=one->uses().front();one->remove_use(edge);
        expect(!IRVerifier().verify(m,error),"broken reverse use rejected");
        one->add_use(edge);expect(IRVerifier().verify(m,error),"restored reverse use");
    }
    expect(int_add(INT32_MAX,1)==INT32_MIN && !int_div_defined(INT32_MIN,-1),"host integer boundaries");
}
void floating_predicates() {
    for (auto predicate : {FCmpPred::ONE, FCmpPred::UNE}) {
        Module m;auto& c=m.type_context();auto* f=function(m);auto* block=f->create_block(c);
        auto* cmp=block->append_instruction(std::make_unique<FCmpInst>(predicate,
            ConstantFloat::get(c,std::numeric_limits<float>::quiet_NaN()),ConstantFloat::get(c,0),c));
        auto* cast=block->append_instruction(std::make_unique<CastInst>(Opcode::ZExt,cmp,c.get_i32_type()));
        auto* ret=static_cast<ReturnInst*>(block->append_instruction(std::make_unique<ReturnInst>(cast,c)));
        ConstFoldingPass().run_on_function(*f);
        auto* result=dynamic_cast<ConstantInt*>(ret->return_value());
        expect(result && result->value()==(predicate==FCmpPred::UNE),"ordered/unordered NaN predicate semantics");
    }
}
void dominance_oracle() {
    std::mt19937 random(20261021);
    for(int trial=0;trial<128;++trial) {
        Module m;auto& c=m.type_context();auto* f=function(m);int n=3+random()%20;
        std::vector<BasicBlock*> blocks;for(int i=0;i<n;++i)blocks.push_back(f->create_block(c));
        for(int i=0;i<n;++i) {
            if(i==n-1) blocks[i]->append_instruction(std::make_unique<ReturnInst>(ConstantInt::get(c,0),c));
            else blocks[i]->append_instruction(std::make_unique<BranchInst>(ConstantInt::get_bool(c,true),blocks[i+1],blocks[1+random()%(n-1)],c));
        }
        DominatorTree tree(*f);
        std::string verifier_error;expect(IRVerifier().verify(m,verifier_error),"random CFG verification");std::vector<std::set<int>> dom(n);dom[0]={0};
        for(int i=1;i<n;++i)for(int j=0;j<n;++j)dom[i].insert(j);
        bool change=true;
        while(change) {
            change=false;
            for(int i=1;i<n;++i) {
                std::set<int> d;bool first=true;
                for(auto* p:blocks[i]->predecessors()) {
                    int k=std::find(blocks.begin(),blocks.end(),p)-blocks.begin();
                    if(first) {d=dom[k];first=false;}
                    else {std::set<int> next;std::set_intersection(d.begin(),d.end(),dom[k].begin(),dom[k].end(),std::inserter(next,next.begin()));d=std::move(next);}
                }
                d.insert(i);if(d!=dom[i]){dom[i]=std::move(d);change=true;}
            }
        }
        std::vector<std::set<int>> frontier(n);
        for(int x=0;x<n;++x)for(int y=0;y<n;++y) {
            expect(tree.dominates(blocks[x],blocks[y])==bool(dom[y].count(x)),"dominates oracle");
            bool df=false;
            for(auto* p:blocks[y]->predecessors()) {int k=std::find(blocks.begin(),blocks.end(),p)-blocks.begin();if(dom[k].count(x)&&(x==y||!dom[y].count(x))) df=true;}
            if(df) frontier[x].insert(y);
            expect(bool(tree.get_dominance_frontier(blocks[x]).count(blocks[y]))==df,"DF oracle");
        }
        std::set<int> pending={int(random()%n),int(random()%n)}, closure;
        std::unordered_set<BasicBlock*> defs;for(int d:pending)defs.insert(blocks[d]);
        while(!pending.empty()) {int d=*pending.begin();pending.erase(pending.begin());for(int x:frontier[d])if(closure.insert(x).second)pending.insert(x);}
        auto idf=tree.compute_idf(defs);
        for(int i=0;i<n;++i)expect(bool(closure.count(i))==bool(idf.count(blocks[i])),"IDF oracle");
    }
}
}
int main() {
    verify_rejections();same_target_phi();lifetime_contexts_and_uses();floating_predicates();dominance_oracle();
    std::cout<<"audit unit regressions: "<<failures<<" failures\n";
    return failures?1:0;
}
