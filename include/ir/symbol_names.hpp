#pragma once
#include "ir/function.hpp"
#include "ir/global_variable.hpp"

namespace sysy {
inline bool compiler_libc_symbol(const std::string& name) {
    return name == "malloc" || name == "free" || name == "exit" || name == "memset";
}
// Runtime helpers have an unspellable source-language prefix. User identifiers
// retain their meaning even when they coincide with an implementation helper.
inline std::string function_symbol(const Function& function) {
    const std::string prefix = ".sysy.runtime.";
    if (function.is_declaration() && function.name().compare(0,prefix.size(),prefix)==0)
        return function.name().substr(prefix.size());
    if (!function.is_declaration() && compiler_libc_symbol(function.name()))
        return ".sysy.user.fn." + function.name();
    return function.name();
}
inline std::string global_symbol(const GlobalVariable& global) {
    return !global.name().empty() && global.name()[0]=='.' ? global.name() : ".sysy.global."+global.name();
}
} // namespace sysy
