#include "ir/function.hpp"
#include "ir/module.hpp"

namespace sysy {

Function::Function(FunctionType* func_type, std::string name, Module* parent, bool is_declaration)
    : Value(func_type, ValueKind::Function, std::move(name)),
      parent_(parent), is_declaration_(is_declaration) {
    for (size_t i = 0; i < func_type->param_types().size(); ++i) {
        args_.push_back(std::make_unique<Argument>(func_type->param_types()[i], this, static_cast<unsigned>(i), "arg" + std::to_string(i)));
    }
}

Function::~Function() {
    for (auto& bb : blocks_)
        for (auto& inst : bb->instructions()) inst->drop_all_operands();
}

FunctionType* Function::function_type() const {
    return static_cast<FunctionType*>(type());
}

Type* Function::return_type() const {
    return function_type()->return_type();
}

BasicBlock* Function::add_block(std::unique_ptr<BasicBlock> bb) {
    bb->set_parent(this);
    auto* ptr = bb.get();
    blocks_.push_back(std::move(bb));
    return ptr;
}

BasicBlock* Function::create_block(TypeContext& ctx, std::string name) {
    if (name.empty()) {
        name = "b" + std::to_string(bb_counter_++);
    } else {
        name = name + "_" + std::to_string(bb_counter_++);
    }
    auto bb = std::make_unique<BasicBlock>(ctx, std::move(name), this);
    auto* ptr = bb.get();
    blocks_.push_back(std::move(bb));
    return ptr;
}

std::unique_ptr<BasicBlock> Function::remove_block(BasicBlock* bb) {
    for (auto it = blocks_.begin(); it != blocks_.end(); ++it) {
        if (it->get() == bb) {
            auto res = std::move(*it);
            blocks_.erase(it);
            res->set_parent(nullptr);
            return res;
        }
    }
    return nullptr;
}

void Function::erase_block(BasicBlock* bb) {
    for (auto it = blocks_.begin(); it != blocks_.end(); ++it) {
        if (it->get() == bb) {
            blocks_.erase(it);
            return;
        }
    }
}

} // namespace sysy
