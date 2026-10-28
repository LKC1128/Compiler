#pragma once

#include "frontend/ast.hpp"
#include "ir/value.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace sysy {

struct Symbol {
    std::string name;
    DataType type = DataType::Int;
    bool is_const = false;
    bool is_array = false;
    bool is_tensor = false;
    bool is_parameter = false;
    std::vector<Value*> runtime_dims;
    std::vector<int> dims;

    // Compile-time evaluation storage for const
    int32_t const_int = 0;
    float const_float = 0.0f;
    std::vector<int32_t> const_int_arr;
    std::vector<float> const_float_arr;

    // IR Value binding (e.g. AllocaInst*, GlobalVariable*, Argument*)
    Value* ir_val = nullptr;
};

class SymbolTable {
public:
    SymbolTable() {
        enter_scope(); // Global scope
    }

    void enter_scope() {
        scopes_.emplace_back();
    }

    void exit_scope() {
        if (scopes_.size() > 1) {
            scopes_.pop_back();
        }
    }

    bool is_global_scope() const {
        return scopes_.size() == 1;
    }

    bool insert(const Symbol& sym) {
        auto& current_scope = scopes_.back();
        if (current_scope.find(sym.name) != current_scope.end()) {
            return false; // Already declared in current scope
        }
        current_scope[sym.name] = sym;
        return true;
    }

    Symbol* lookup(const std::string& name) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                return &found->second;
            }
        }
        return nullptr;
    }

    const Symbol* lookup(const std::string& name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                return &found->second;
            }
        }
        return nullptr;
    }

private:
    std::vector<std::unordered_map<std::string, Symbol>> scopes_;
};

} // namespace sysy
