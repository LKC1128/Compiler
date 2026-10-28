#pragma once
#include "ir/type.hpp"
namespace sysy {
struct ArgLocation {
    enum Kind { FloatRegister, IntegerRegister, Stack } kind;
    int index;
};
class ArgAllocator {
public:
    // AAPCS64 Linux: independent GPR/FP banks, including unnamed arguments.
    // Exhausted floating-point arguments go to the stack, never to GPRs.
    ArgLocation next(Type* type, bool named = true) {
        (void)named;
        if(type->is_float()) {
            if(next_float_<8) return {ArgLocation::FloatRegister,next_float_++};
        } else if(next_integer_<8) return {ArgLocation::IntegerRegister,next_integer_++};
        int offset=stack_bytes_;stack_bytes_+=8;return {ArgLocation::Stack,offset};
    }
    int stack_bytes() const {return stack_bytes_;}
private:
    int next_integer_=0,next_float_=0,stack_bytes_=0;
};
}
