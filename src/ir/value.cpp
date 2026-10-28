#include "ir/value.hpp"
#include "ir/user.hpp"

namespace sysy {

void Use::set_value(Value* val) {
    if (val_ == val) return;
    if (val_) {
        val_->remove_use(this);
    }
    val_ = val;
    if (val_) {
        val_->add_use(this);
    }
}

void Value::replace_all_uses_with(Value* new_val) {
    if (this == new_val) return;
    // Copy uses vector because set_value modifies this->uses_
    std::vector<Use*> current_uses = uses_;
    for (Use* u : current_uses) {
        u->set_value(new_val);
    }
}

} // namespace sysy
