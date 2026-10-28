#pragma once

#include "ir/value.hpp"
#include "ir/basic_block.hpp"
#include <vector>
#include <list>
#include <memory>
#include <string>

namespace sysy {

class Module;

class Argument : public Value {
public:
    Argument(Type* type, Function* parent, unsigned arg_no, std::string name = "")
        : Value(type, ValueKind::Argument, std::move(name)), parent_(parent), arg_no_(arg_no) {}

    Function* parent() const { return parent_; }
    unsigned arg_no() const { return arg_no_; }

private:
    Function* parent_;
    unsigned arg_no_;
};

class Function : public Value {
public:
    Function(FunctionType* func_type, std::string name, Module* parent = nullptr, bool is_declaration = false);
    ~Function() override;

    IRLevel level() const { return level_; }
    unsigned state_flags() const { return state_; }
    void set_representation(IRLevel level, unsigned state) {
        level_=level; state_=state;
        for (auto& b:blocks_) for(auto& i:b->instructions()) {
            i->set_representation(level,state);
            if(level!=IRLevel::HIR) i->set_region_id(-1);
        }
    }
    std::vector<StructuredRegion>& regions() { return regions_; }
    const std::vector<StructuredRegion>& regions() const { return regions_; }
    int active_region() const { return region_stack_.empty() ? -1 : region_stack_.back(); }
    int begin_region(std::string kind,BasicBlock* entry,BasicBlock* exit=nullptr) {
        int id=static_cast<int>(regions_.size());
        regions_.push_back({std::move(kind),active_region(),entry,exit});
        region_stack_.push_back(id); return id;
    }
    void end_region() { region_stack_.pop_back(); }
    void clear_regions() { regions_.clear(); region_stack_.clear(); }
    MachineLayout& machine_layout() { return layout_; }
    const MachineLayout& machine_layout() const { return layout_; }
    Module* parent() const { return parent_; }
    void set_parent(Module* parent) { parent_ = parent; }

    FunctionType* function_type() const;
    Type* return_type() const;

    bool is_declaration() const { return is_declaration_; }
    void set_is_declaration(bool is_decl) { is_declaration_ = is_decl; }

    const std::vector<std::unique_ptr<Argument>>& args() const { return args_; }
    Argument* get_arg(size_t i) const { return args_[i].get(); }

    const std::list<std::unique_ptr<BasicBlock>>& blocks() const { return blocks_; }
    std::list<std::unique_ptr<BasicBlock>>& blocks() { return blocks_; }

    BasicBlock* get_entry_block() const {
        return blocks_.empty() ? nullptr : blocks_.front().get();
    }

    BasicBlock* add_block(std::unique_ptr<BasicBlock> bb);
    BasicBlock* create_block(TypeContext& ctx, std::string name = "");
    std::unique_ptr<BasicBlock> remove_block(BasicBlock* bb);
    void erase_block(BasicBlock* bb);

private:
    IRLevel level_=IRLevel::MIR;
    unsigned state_=SF_SSA;
    std::vector<StructuredRegion> regions_;
    std::vector<int> region_stack_;
    MachineLayout layout_;
    Module* parent_;
    bool is_declaration_;
    int bb_counter_ = 0;
    std::vector<std::unique_ptr<Argument>> args_;
    std::list<std::unique_ptr<BasicBlock>> blocks_;
};

} // namespace sysy
