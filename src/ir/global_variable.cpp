#include "ir/global_variable.hpp"
#include "ir/module.hpp"

namespace sysy {

GlobalVariable::GlobalVariable(Type* type, bool is_constant, Constant* init_val, std::string name, Module* parent)
    : User(parent ? parent->type_context().get_pointer_type(type) : nullptr, ValueKind::GlobalVariable, std::move(name)),
      is_constant_(is_constant), parent_(parent) {
    if (init_val) {
        add_operand(init_val);
    }
}

Constant* GlobalVariable::initializer() const {
    if (num_operands() > 0) {
        return static_cast<Constant*>(get_operand(0));
    }
    return nullptr;
}

void GlobalVariable::set_initializer(Constant* init) {
    if (num_operands() == 0) {
        add_operand(init);
    } else {
        set_operand(0, init);
    }
}

} // namespace sysy
