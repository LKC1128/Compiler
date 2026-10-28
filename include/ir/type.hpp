#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstddef>
#include <map>
#include <tuple>
#include <cstdint>

namespace sysy {

class Constant;
class TypeContext;

enum class TypeKind {
    Void,
    Integer,
    Float,
    Pointer,
    Array,
    Function
};

class Type {
public:
    explicit Type(TypeKind kind) : kind_(kind) {}
    virtual ~Type() = default;

    TypeContext* context() const { return context_; }
    void set_context(TypeContext* ctx) { context_ = ctx; }

    TypeKind kind() const { return kind_; }
    bool is_void() const { return kind_ == TypeKind::Void; }
    bool is_integer() const { return kind_ == TypeKind::Integer; }
    bool is_float() const { return kind_ == TypeKind::Float; }
    bool is_pointer() const { return kind_ == TypeKind::Pointer; }
    bool is_array() const { return kind_ == TypeKind::Array; }
    bool is_function() const { return kind_ == TypeKind::Function; }

    virtual std::string to_string() const = 0;
    virtual size_t size_in_bytes() const = 0;

private:
    TypeKind kind_;
    TypeContext* context_ = nullptr;
};

class VoidType : public Type {
public:
    VoidType() : Type(TypeKind::Void) {}
    std::string to_string() const override { return "void"; }
    size_t size_in_bytes() const override { return 0; }
};

class IntegerType : public Type {
public:
    explicit IntegerType(unsigned bits) : Type(TypeKind::Integer), bits_(bits) {}
    unsigned bits() const { return bits_; }
    std::string to_string() const override { return "i" + std::to_string(bits_); }
    size_t size_in_bytes() const override { return (bits_ + 7) / 8; }

private:
    unsigned bits_;
};

class FloatType : public Type {
public:
    explicit FloatType(unsigned bits = 32) : Type(TypeKind::Float), bits_(bits) {}
    std::string to_string() const override { return bits_ == 64 ? "double" : "float"; }
    size_t size_in_bytes() const override { return bits_ / 8; }
private:
    unsigned bits_;
};

class PointerType : public Type {
public:
    explicit PointerType(Type* pointee) : Type(TypeKind::Pointer), pointee_(pointee) {}
    Type* pointee() const { return pointee_; }
    std::string to_string() const override { return pointee_->to_string() + "*"; }
    size_t size_in_bytes() const override { return 8; } // 64-bit pointer on RV64

private:
    Type* pointee_;
};

class ArrayType : public Type {
public:
    ArrayType(Type* element_type, size_t num_elements)
        : Type(TypeKind::Array), element_type_(element_type), num_elements_(num_elements) {}

    Type* element_type() const { return element_type_; }
    size_t num_elements() const { return num_elements_; }

    std::string to_string() const override {
        return "[" + std::to_string(num_elements_) + " x " + element_type_->to_string() + "]";
    }
    size_t size_in_bytes() const override {
        return num_elements_ * element_type_->size_in_bytes();
    }

private:
    Type* element_type_;
    size_t num_elements_;
};

class FunctionType : public Type {
public:
    FunctionType(Type* return_type, std::vector<Type*> param_types, bool variadic = false)
        : Type(TypeKind::Function), return_type_(return_type), param_types_(std::move(param_types)), variadic_(variadic) {}

    bool is_variadic() const { return variadic_; }
    Type* return_type() const { return return_type_; }
    const std::vector<Type*>& param_types() const { return param_types_; }

    std::string to_string() const override;
    size_t size_in_bytes() const override { return 0; }

private:
    Type* return_type_;
    std::vector<Type*> param_types_;
    bool variadic_;
};

class TypeContext {
public:
    TypeContext();
    ~TypeContext();

    VoidType* get_void_type() { return void_ty_.get(); }
    IntegerType* get_i8_type() { return i8_ty_.get(); }
    FloatType* get_double_type() { return double_ty_.get(); }
    IntegerType* get_i1_type() { return i1_ty_.get(); }
    IntegerType* get_i32_type() { return i32_ty_.get(); }
    FloatType* get_float_type() { return float_ty_.get(); }

    Constant* intern_constant(Type* type, int kind, uint32_t bits,
                              std::unique_ptr<Constant> value);
    Constant* own_constant(std::unique_ptr<Constant> value);
    const std::vector<std::unique_ptr<Constant>>& constants() const { return constants_; }

    PointerType* get_pointer_type(Type* pointee);
    ArrayType* get_array_type(Type* element_type, size_t num_elements);
    FunctionType* get_function_type(Type* ret_type, std::vector<Type*> params, bool variadic = false);

private:
    std::unique_ptr<VoidType> void_ty_;
    std::unique_ptr<IntegerType> i1_ty_;
    std::unique_ptr<IntegerType> i8_ty_;
    std::unique_ptr<FloatType> double_ty_;
    std::unique_ptr<IntegerType> i32_ty_;
    std::unique_ptr<FloatType> float_ty_;
    std::vector<std::unique_ptr<PointerType>> pointer_types_;
    std::vector<std::unique_ptr<ArrayType>> array_types_;
    std::vector<std::unique_ptr<FunctionType>> func_types_;
    std::map<std::tuple<Type*, int, uint32_t>, Constant*> constant_cache_;
    std::vector<std::unique_ptr<Constant>> constants_;
};

} // namespace sysy
