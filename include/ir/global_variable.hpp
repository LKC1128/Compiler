#pragma once

#include "ir/user.hpp"
#include "ir/constant.hpp"

namespace sysy {

class Module;

class GlobalVariable : public User {
public:
    GlobalVariable(Type* type, bool is_constant, Constant* init_val, std::string name, Module* parent = nullptr);

    bool is_constant() const { return is_constant_; }
    Constant* initializer() const;
    void set_initializer(Constant* init);

    Module* parent() const { return parent_; }
    void set_parent(Module* parent) { parent_ = parent; }

private:
    bool is_constant_;
    Module* parent_;
};

} // namespace sysy
