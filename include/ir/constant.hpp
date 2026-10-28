#pragma once

#include "ir/user.hpp"
#include <vector>
#include <string>

namespace sysy {

class Constant : public User {
public:
    Constant(Type* type, ValueKind kind) : User(type, kind) {}
    virtual bool is_zero() const = 0;
};

class ConstantInt : public Constant {
public:
    ConstantInt(Type* type, int32_t value)
        : Constant(type, ValueKind::ConstantInt), value_(value) {}

    int32_t value() const { return value_; }
    bool is_zero() const override { return value_ == 0; }

    static ConstantInt* get(TypeContext& ctx, int32_t val);
    static ConstantInt* get_byte(TypeContext& ctx, uint8_t val);
    static ConstantInt* get_bool(TypeContext& ctx, bool val);

private:
    int32_t value_;
};

class ConstantFloat : public Constant {
public:
    ConstantFloat(Type* type, float value)
        : Constant(type, ValueKind::ConstantFloat), value_(value) {}

    float value() const { return value_; }
    bool is_zero() const override { return value_ == 0.0f; }

    static ConstantFloat* get(TypeContext& ctx, float val);

private:
    float value_;
};

class ConstantZero : public Constant {
public:
    explicit ConstantZero(Type* type)
        : Constant(type, ValueKind::ConstantZero) {}

    bool is_zero() const override { return true; }

    static ConstantZero* get(Type* type);
};

class ConstantArray : public Constant {
public:
    ConstantArray(ArrayType* type, std::vector<Constant*> elements);

    const std::vector<Constant*>& elements() const { return elements_; }
    bool is_zero() const override;

    Constant* get_element(size_t index) const {
        return elements_[index];
    }

private:
    std::vector<Constant*> elements_;
};

class PoisonValue : public Constant {
public:
    explicit PoisonValue(Type* type):Constant(type,ValueKind::PoisonValue) {}
    bool is_zero() const override { return false; }
    static PoisonValue* get(Type* type);
};
class UndefValue : public Constant {
public:
    explicit UndefValue(Type* type)
        : Constant(type, ValueKind::UndefValue) {}

    bool is_zero() const override { return false; }

    static UndefValue* get(Type* type);
};

} // namespace sysy
