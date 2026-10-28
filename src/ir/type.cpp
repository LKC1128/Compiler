#include "ir/type.hpp"
#include "ir/constant.hpp"
#include <sstream>

namespace sysy {

std::string FunctionType::to_string() const {
    std::ostringstream oss;
    oss << return_type_->to_string() << " (";
    for (size_t i = 0; i < param_types_.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << param_types_[i]->to_string();
    }
    if (variadic_) oss << (param_types_.empty() ? "..." : ", ...");
    oss << ")";
    return oss.str();
}

TypeContext::TypeContext() {
    void_ty_ = std::make_unique<VoidType>();
    i1_ty_ = std::make_unique<IntegerType>(1);
    i8_ty_ = std::make_unique<IntegerType>(8);
    double_ty_ = std::make_unique<FloatType>(64);
    i32_ty_ = std::make_unique<IntegerType>(32);
    float_ty_ = std::make_unique<FloatType>();
    for (Type* ty : std::vector<Type*>{void_ty_.get(), i1_ty_.get(), i32_ty_.get(), float_ty_.get(), i8_ty_.get(), double_ty_.get()}) ty->set_context(this);
}

TypeContext::~TypeContext() {
    for (auto& value : constants_) value->drop_all_operands();
}

Constant* TypeContext::own_constant(std::unique_ptr<Constant> value) {
    auto* ptr = value.get();
    constants_.push_back(std::move(value));
    return ptr;
}

Constant* TypeContext::intern_constant(Type* type, int kind, uint32_t bits,
                                     std::unique_ptr<Constant> value) {
    auto key = std::make_tuple(type, kind, bits);
    auto it = constant_cache_.find(key);
    if (it != constant_cache_.end()) return it->second;
    auto* ptr = own_constant(std::move(value));
    constant_cache_.emplace(key, ptr);
    return ptr;
}

PointerType* TypeContext::get_pointer_type(Type* pointee) {
    for (auto& pt : pointer_types_) {
        if (pt->pointee() == pointee) return pt.get();
    }
    pointer_types_.push_back(std::make_unique<PointerType>(pointee));
    pointer_types_.back()->set_context(this);
    return pointer_types_.back().get();
}

ArrayType* TypeContext::get_array_type(Type* element_type, size_t num_elements) {
    for (auto& at : array_types_) {
        if (at->element_type() == element_type && at->num_elements() == num_elements) {
            return at.get();
        }
    }
    array_types_.push_back(std::make_unique<ArrayType>(element_type, num_elements));
    array_types_.back()->set_context(this);
    return array_types_.back().get();
}

FunctionType* TypeContext::get_function_type(Type* ret_type, std::vector<Type*> params, bool variadic) {
    for (auto& ft : func_types_) {
        if (ft->return_type() == ret_type && ft->param_types() == params && ft->is_variadic() == variadic) {
            return ft.get();
        }
    }
    func_types_.push_back(std::make_unique<FunctionType>(ret_type, std::move(params), variadic));
    func_types_.back()->set_context(this);
    return func_types_.back().get();
}

} // namespace sysy
