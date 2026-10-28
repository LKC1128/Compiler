#pragma once

#include "ir/module.hpp"
#include <ostream>
#include <unordered_map>

namespace sysy {

class IRPrinter {
public:
    explicit IRPrinter(std::ostream& os) : os_(os) {}

    void print(const Module& module);
    void print(const Function& function);
    void print(const BasicBlock& block);
    void print(const Instruction& inst);
    void print(const Value* value);

private:
    std::ostream& os_;
    std::unordered_map<const Value*, std::string> value_names_;
    int unnamed_counter_ = 0;

    void build_value_names(const Function& function);
    std::string get_value_name(const Value* val);
    std::string format_constant(const Constant* c);
};

} // namespace sysy
