#pragma once

#include "ir/type.hpp"
#include "ir/use.hpp"
#include <string>
#include <vector>
#include <algorithm>

namespace sysy {

class Value {
public:
    enum class ValueKind {
        Instruction,
        Argument,
        BasicBlock,
        ConstantInt,
        ConstantFloat,
        ConstantZero,
        ConstantArray,
        UndefValue,
        PoisonValue,
        GlobalVariable,
        Function
    };

    Value(Type* type, ValueKind kind, std::string name = "")
        : type_(type), kind_(kind), name_(std::move(name)) {}

    virtual ~Value() = default;

    Type* type() const { return type_; }
    void set_type(Type* type) { type_ = type; }
    ValueKind kind() const { return kind_; }
    const std::string& name() const { return name_; }
    void set_name(std::string name) { name_ = std::move(name); }
    bool has_name() const { return !name_.empty(); }

    const std::vector<Use*>& uses() const { return uses_; }
    bool has_uses() const { return !uses_.empty(); }

    void add_use(Use* u) {
        uses_.push_back(u);
    }

    void remove_use(Use* u) {
        auto it = std::find(uses_.begin(), uses_.end(), u);
        if (it != uses_.end()) {
            uses_.erase(it);
        }
    }

    void replace_all_uses_with(Value* new_val);

private:
    Type* type_;
    ValueKind kind_;
    std::string name_;
    std::vector<Use*> uses_;
};

} // namespace sysy
