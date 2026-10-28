#pragma once

#include "ir/module.hpp"
#include <string>
#include <vector>

namespace sysy {

class IRVerifier {
public:
    IRVerifier() = default;

    bool verify(const Module& module, std::string& error_msg);
    bool verify(const Function& function, std::string& error_msg);
    bool verify(const BasicBlock& bb, std::string& error_msg);
    bool verify(const Instruction& inst, std::string& error_msg);

private:
    void check_function(const Function& fn);
    void check_instruction(const Instruction& inst);
    void check_uses(const Module& module);
    std::string err_;
    void report_error(const std::string& msg);
};

} // namespace sysy
