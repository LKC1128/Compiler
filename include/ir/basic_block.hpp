#pragma once

#include "ir/value.hpp"
#include "ir/instruction.hpp"
#include <list>
#include <vector>
#include <memory>

namespace sysy {

class Function;

class BasicBlock : public Value {
public:
    BasicBlock(TypeContext& ctx, std::string name = "", Function* parent = nullptr);
    ~BasicBlock() override;

    Function* parent() const { return parent_; }
    void set_parent(Function* parent) { parent_ = parent; }

    const std::list<std::unique_ptr<Instruction>>& instructions() const {
        return instructions_;
    }

    std::list<std::unique_ptr<Instruction>>& instructions() {
        return instructions_;
    }

    Instruction* append_instruction(std::unique_ptr<Instruction> inst);
    Instruction* insert_before(Instruction* pos, std::unique_ptr<Instruction> inst);
    Instruction* insert_front(std::unique_ptr<Instruction> inst);
    std::unique_ptr<Instruction> remove_instruction(Instruction* inst);
    void erase_instruction(Instruction* inst);

    Instruction* get_terminator() const;
    bool has_terminator() const { return get_terminator() != nullptr; }

    const std::vector<BasicBlock*>& predecessors() const { return preds_; }
    const std::vector<BasicBlock*>& successors() const { return succs_; }

    void add_predecessor(BasicBlock* bb);
    void remove_predecessor(BasicBlock* bb);
    void add_successor(BasicBlock* bb);
    void remove_successor(BasicBlock* bb);
    void clear_successors();
    void clear_predecessors();

private:
    Function* parent_;
    std::list<std::unique_ptr<Instruction>> instructions_;
    std::vector<BasicBlock*> preds_;
    std::vector<BasicBlock*> succs_;
};

} // namespace sysy
