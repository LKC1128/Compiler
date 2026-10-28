#pragma once

#include <cstddef>

namespace sysy {

class Value;
class User;

class Use {
public:
    Use(User* user, Value* val, size_t operand_index)
        : user_(user), val_(val), operand_index_(operand_index) {}

    User* user() const { return user_; }
    Value* value() const { return val_; }
    size_t operand_index() const { return operand_index_; }

    void set_value(Value* val);

private:
    User* user_;
    Value* val_;
    size_t operand_index_;
};

} // namespace sysy
