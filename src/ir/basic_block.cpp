#include "ir/basic_block.hpp"
#include "ir/function.hpp"
#include <algorithm>

namespace sysy {

BasicBlock::BasicBlock(TypeContext& ctx, std::string name, Function* parent)
    : Value(ctx.get_void_type(), ValueKind::BasicBlock, std::move(name)), parent_(parent) {}

BasicBlock::~BasicBlock() {
    for (auto& inst : instructions_) inst->drop_all_operands();
}

Instruction* BasicBlock::append_instruction(std::unique_ptr<Instruction> inst) {
    inst->set_parent(this);
    if(parent_) { inst->set_representation(parent_->level(),parent_->state_flags()); inst->set_region_id(parent_->active_region()); }
    auto* ptr = inst.get();
    instructions_.push_back(std::move(inst));
    return ptr;
}

Instruction* BasicBlock::insert_before(Instruction* pos, std::unique_ptr<Instruction> inst) {
    inst->set_parent(this);
    if(parent_) { inst->set_representation(parent_->level(),parent_->state_flags()); inst->set_region_id(parent_->active_region()); }
    auto* ptr = inst.get();
    for (auto it = instructions_.begin(); it != instructions_.end(); ++it) {
        if (it->get() == pos) {
            instructions_.insert(it, std::move(inst));
            return ptr;
        }
    }
    instructions_.push_back(std::move(inst));
    return ptr;
}

Instruction* BasicBlock::insert_front(std::unique_ptr<Instruction> inst) {
    inst->set_parent(this);
    if(parent_) { inst->set_representation(parent_->level(),parent_->state_flags()); inst->set_region_id(parent_->active_region()); }
    auto* ptr = inst.get();
    // If there are existing phi instructions at the front, insert after the last phi, or at the very front
    instructions_.push_front(std::move(inst));
    return ptr;
}

std::unique_ptr<Instruction> BasicBlock::remove_instruction(Instruction* inst) {
    for (auto it = instructions_.begin(); it != instructions_.end(); ++it) {
        if (it->get() == inst) {
            auto res = std::move(*it);
            instructions_.erase(it);
            res->set_parent(nullptr);
            return res;
        }
    }
    return nullptr;
}

void BasicBlock::erase_instruction(Instruction* inst) {
    for (auto it = instructions_.begin(); it != instructions_.end(); ++it) {
        if (it->get() == inst) {
            instructions_.erase(it);
            return;
        }
    }
}

Instruction* BasicBlock::get_terminator() const {
    if (instructions_.empty()) return nullptr;
    if (instructions_.back()->is_terminator()) {
        return instructions_.back().get();
    }
    return nullptr;
}

void BasicBlock::add_predecessor(BasicBlock* bb) {
    if (std::find(preds_.begin(), preds_.end(), bb) == preds_.end()) {
        preds_.push_back(bb);
    }
}

void BasicBlock::remove_predecessor(BasicBlock* bb) {
    auto it = std::find(preds_.begin(), preds_.end(), bb);
    if (it != preds_.end()) {
        preds_.erase(it);
    }
}

void BasicBlock::add_successor(BasicBlock* bb) {
    if (std::find(succs_.begin(), succs_.end(), bb) == succs_.end()) {
        succs_.push_back(bb);
    }
}

void BasicBlock::remove_successor(BasicBlock* bb) {
    auto it = std::find(succs_.begin(), succs_.end(), bb);
    if (it != succs_.end()) {
        succs_.erase(it);
    }
}

void BasicBlock::clear_successors() {
    succs_.clear();
}

void BasicBlock::clear_predecessors() {
    preds_.clear();
}

} // namespace sysy
