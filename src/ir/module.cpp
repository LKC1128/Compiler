#include "ir/module.hpp"

namespace sysy {

Module::~Module() {
    for (auto& gv : globals_) gv->drop_all_operands();
    for (auto& fn : functions_)
        for (auto& bb : fn->blocks())
            for (auto& inst : bb->instructions()) inst->drop_all_operands();
}

GlobalVariable* Module::add_global(std::unique_ptr<GlobalVariable> gv) {
    gv->set_parent(this);
    if (!gv->type()) {
        Type* pointee = gv->initializer() ? gv->initializer()->type() : type_ctx_.get_i32_type();
        gv->set_type(type_ctx_.get_pointer_type(pointee));
    }
    auto* ptr = gv.get();
    globals_.push_back(std::move(gv));
    return ptr;
}

Function* Module::add_function(std::unique_ptr<Function> fn) {
    fn->set_parent(this);
    auto* ptr = fn.get();
    functions_.push_back(std::move(fn));
    return ptr;
}

Function* Module::get_function(const std::string& name) const {
    for (const auto& fn : functions_) {
        if (fn->name() == name) return fn.get();
    }
    return nullptr;
}

GlobalVariable* Module::get_global(const std::string& name) const {
    for (const auto& gv : globals_) {
        if (gv->name() == name) return gv.get();
    }
    return nullptr;
}

void Module::register_runtime_functions() {
    auto* void_ty = type_ctx_.get_void_type();
    auto* i32_ty = type_ctx_.get_i32_type();
    auto* float_ty = type_ctx_.get_float_type();
    auto* i32_ptr_ty = type_ctx_.get_pointer_type(i32_ty);
    auto* float_ptr_ty = type_ctx_.get_pointer_type(float_ty);

    // getint() -> i32
    if (!get_function("getint"))
        add_function(std::make_unique<Function>(type_ctx_.get_function_type(i32_ty, {}), "getint", this, true));

    // getch() -> i32
    if (!get_function("getch"))
        add_function(std::make_unique<Function>(type_ctx_.get_function_type(i32_ty, {}), "getch", this, true));

    // getfloat() -> float
    if (!get_function("getfloat"))
        add_function(std::make_unique<Function>(type_ctx_.get_function_type(float_ty, {}), "getfloat", this, true));

    // getarray(i32*) -> i32
    if (!get_function("getarray"))
        add_function(std::make_unique<Function>(type_ctx_.get_function_type(i32_ty, {i32_ptr_ty}), "getarray", this, true));

    // getfarray(float*) -> i32
    if (!get_function("getfarray"))
        add_function(std::make_unique<Function>(type_ctx_.get_function_type(i32_ty, {float_ptr_ty}), "getfarray", this, true));

    // putint(i32) -> void
    if (!get_function("putint"))
        add_function(std::make_unique<Function>(type_ctx_.get_function_type(void_ty, {i32_ty}), "putint", this, true));

    // putch(i32) -> void
    if (!get_function("putch"))
        add_function(std::make_unique<Function>(type_ctx_.get_function_type(void_ty, {i32_ty}), "putch", this, true));

    // putfloat(float) -> void
    if (!get_function("putfloat"))
        add_function(std::make_unique<Function>(type_ctx_.get_function_type(void_ty, {float_ty}), "putfloat", this, true));

    // putarray(i32, i32*) -> void
    if (!get_function("putarray"))
        add_function(std::make_unique<Function>(type_ctx_.get_function_type(void_ty, {i32_ty, i32_ptr_ty}), "putarray", this, true));

    // putfarray(i32, float*) -> void
    if (!get_function("putfarray"))
        add_function(std::make_unique<Function>(type_ctx_.get_function_type(void_ty, {i32_ty, float_ptr_ty}), "putfarray", this, true));

    if (!get_function("putf"))
        add_function(std::make_unique<Function>(type_ctx_.get_function_type(void_ty, {type_ctx_.get_pointer_type(type_ctx_.get_i8_type())}, true), "putf", this, true));

    add_function(std::make_unique<Function>(type_ctx_.get_function_type(void_ty, {i32_ty}), "_sysy_starttime", this, true));
    add_function(std::make_unique<Function>(type_ctx_.get_function_type(void_ty, {i32_ty}), "_sysy_stoptime", this, true));

    add_function(std::make_unique<Function>(type_ctx_.get_function_type(i32_ptr_ty, {i32_ty}), ".sysy.runtime.malloc", this, true));
    add_function(std::make_unique<Function>(type_ctx_.get_function_type(void_ty, {i32_ptr_ty}), ".sysy.runtime.free", this, true));
    add_function(std::make_unique<Function>(type_ctx_.get_function_type(void_ty, {i32_ty}), ".sysy.runtime.exit", this, true));

    // memset(i32*, i32, i32) -> void
    if (!get_function(".sysy.runtime.memset"))
        add_function(std::make_unique<Function>(type_ctx_.get_function_type(void_ty, {i32_ptr_ty, i32_ty, i32_ty}), ".sysy.runtime.memset", this, true));
}

} // namespace sysy
