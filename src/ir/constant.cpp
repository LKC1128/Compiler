#include "ir/constant.hpp"
#include "ir/bit_utils.hpp"

namespace sysy {

ConstantInt* ConstantInt::get(TypeContext& ctx, int32_t val) {
    auto* ty = ctx.get_i32_type();
    return static_cast<ConstantInt*>(ctx.intern_constant(ty, 0, static_cast<uint32_t>(val), std::make_unique<ConstantInt>(ty, val)));
}
ConstantInt* ConstantInt::get_byte(TypeContext& ctx, uint8_t val) {
    auto* ty = ctx.get_i8_type();
    return static_cast<ConstantInt*>(ctx.intern_constant(ty, 0, val, std::make_unique<ConstantInt>(ty, val)));
}
ConstantInt* ConstantInt::get_bool(TypeContext& ctx, bool val) {
    auto* ty = ctx.get_i1_type();
    return static_cast<ConstantInt*>(ctx.intern_constant(ty, 0, val, std::make_unique<ConstantInt>(ty, val)));
}
ConstantFloat* ConstantFloat::get(TypeContext& ctx, float val) {
    auto* ty = ctx.get_float_type();
    return static_cast<ConstantFloat*>(ctx.intern_constant(ty, 1, bit_copy<uint32_t>(val), std::make_unique<ConstantFloat>(ty, val)));
}
ConstantZero* ConstantZero::get(Type* type) {
    return static_cast<ConstantZero*>(type->context()->intern_constant(type, 2, 0, std::make_unique<ConstantZero>(type)));
}
PoisonValue* PoisonValue::get(Type* type) {
    return static_cast<PoisonValue*>(type->context()->intern_constant(type,4,0,std::make_unique<PoisonValue>(type)));
}
UndefValue* UndefValue::get(Type* type) {
    return static_cast<UndefValue*>(type->context()->intern_constant(type, 3, 0, std::make_unique<UndefValue>(type)));
}

ConstantArray::ConstantArray(ArrayType* type, std::vector<Constant*> elements)
    : Constant(type, ValueKind::ConstantArray), elements_(std::move(elements)) {
    for (auto* elem : elements_) {
        add_operand(elem);
    }
}

bool ConstantArray::is_zero() const {
    for (auto* elem : elements_) {
        if (!elem->is_zero()) return false;
    }
    return true;
}

} // namespace sysy
