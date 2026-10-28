#pragma once

#include "ir/type.hpp"
#include "ir/function.hpp"
#include "ir/global_variable.hpp"
#include <string>
#include <vector>
#include <memory>

namespace sysy {

class Module {
public:
    explicit Module(std::string name = "sysy_module")
        : name_(std::move(name)) {}

    ~Module();

    const std::string& name() const { return name_; }
    TypeContext& type_context() { return type_ctx_; }
    const TypeContext& type_context() const { return type_ctx_; }

    const std::vector<std::unique_ptr<GlobalVariable>>& globals() const { return globals_; }
    std::vector<std::unique_ptr<GlobalVariable>>& globals() { return globals_; }

    const std::vector<std::unique_ptr<Function>>& functions() const { return functions_; }
    std::vector<std::unique_ptr<Function>>& functions() { return functions_; }

    GlobalVariable* add_global(std::unique_ptr<GlobalVariable> gv);
    Function* add_function(std::unique_ptr<Function> fn);

    Function* get_function(const std::string& name) const;
    GlobalVariable* get_global(const std::string& name) const;

    // Helper to register standard SysY runtime functions
    void register_runtime_functions();

private:
    std::string name_;
    TypeContext type_ctx_;
    std::vector<std::unique_ptr<GlobalVariable>> globals_;
    std::vector<std::unique_ptr<Function>> functions_;
};

} // namespace sysy
