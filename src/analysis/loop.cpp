#include "analysis/loop.hpp"
#include "analysis/cfg.hpp"
#include <algorithm>
#include <map>
namespace sysy {
std::vector<NaturalLoop> find_natural_loops(Function& fn) {
    DominatorTree dom(fn);std::vector<NaturalLoop> result;std::map<BasicBlock*,size_t> indices;
    for(auto& b:fn.blocks()) for(auto* h:b->successors()) if(dom.dominates(h,b.get())) {
        if(!indices.count(h)) {indices[h]=result.size();result.push_back({h,{h},{}});}
        auto& loop=result[indices[h]];loop.latches.push_back(b.get());
        std::vector<BasicBlock*> work;
        if(loop.blocks.insert(b.get()).second) work.push_back(b.get());
        while(!work.empty()) {auto* p=work.back();work.pop_back();
            for(auto* prev:p->predecessors()) if(loop.blocks.insert(prev).second) work.push_back(prev);
        }
    }
    std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.blocks.size()<b.blocks.size();});
    return result;
}
BasicBlock* ensure_preheader(Function& fn,const NaturalLoop& loop,TypeContext& ctx) {
    std::vector<BasicBlock*> outside;
    for(auto* p:loop.header->predecessors()) if(!loop.blocks.count(p)) outside.push_back(p);
    if(outside.empty()) return nullptr;
    if(outside.size()==1 && outside[0]->successors().size()==1) return outside[0];
    auto* pre=fn.create_block(ctx,"loop.preheader");
    for(auto& owner:loop.header->instructions()) {
        auto* phi=dynamic_cast<PhiInst*>(owner.get());if(!phi) break;
        auto merge=std::make_unique<PhiInst>(phi->type());
        for(auto* p:outside) merge->add_incoming(phi->get_incoming_value_for_block(p),p);
        Value* init=outside.size()==1 ? merge->incoming_value(0) : pre->append_instruction(std::move(merge));
        for(auto* p:outside) phi->remove_incoming_block(p);
        phi->add_incoming(init,pre);
    }
    for(auto* p:outside) {
        auto* br=static_cast<BranchInst*>(p->get_terminator());
        if(!br->is_conditional()) br->set_target(pre);
        else {if(br->true_target()==loop.header)br->set_true_target(pre);if(br->false_target()==loop.header)br->set_false_target(pre);}
    }
    pre->append_instruction(std::make_unique<BranchInst>(loop.header,ctx));CFGAnalysis::compute_cfg(fn);return pre;
}
}
