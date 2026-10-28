#pragma once

#include "ir/value.hpp"
#include <vector>
#include <memory>
#include <cassert>

namespace sysy {

class User : public Value {
public:
    User(Type* type, ValueKind kind, std::string name = "")
        : Value(type, kind, std::move(name)) {}

    ~User() override {
        drop_all_operands();
    }

    size_t num_operands() const { return operands_.size(); }

    Value* get_operand(size_t index) const {
        assert(index < operands_.size());
        return operands_[index]->value();
    }

    void set_operand(size_t index, Value* val) {
        assert(index < operands_.size());
        operands_[index]->set_value(val);
    }

    void add_operand(Value* val) {
        size_t idx = operands_.size();
        auto use = std::make_unique<Use>(this, val, idx);
        if (val) {
            val->add_use(use.get());
        }
        operands_.push_back(std::move(use));
    }

    void drop_all_operands() {
        for (auto& use : operands_) {
            if (use && use->value()) {
                use->value()->remove_use(use.get());
            }
        }
        operands_.clear();
    }

    const std::vector<std::unique_ptr<Use>>& operand_uses() const {
        return operands_;
    }

protected:
    std::vector<std::unique_ptr<Use>> operands_;
};

} // namespace sysy
