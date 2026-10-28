#pragma once
#include "ir/module.hpp"
#include <ostream>
#include <string>
namespace sysy {
// Independent ARMv8-A/AAPCS64 code generation over the shared allocated LIR.
class ARM64CodeGen {
public:
    explicit ARM64CodeGen(std::ostream& output):out_(output) {}
    void generate(const Module& module);
private:
    std::ostream& out_;
    const Function* function_=nullptr;
    size_t branch_id_=0;
    void globals(const Module& module);
    void initializer(const Constant* value,Type* type);
    void function(const Function& fn);
    void instruction(const Instruction& inst);
    void immediate(const std::string& reg,int64_t value);
    void address(const std::string& dst,const std::string& base,int64_t offset);
    void memory(const std::string& op,const std::string& reg,const std::string& base,int64_t offset);
    void get(const Value* value,const std::string& reg);
    void put(const std::string& reg,const Value* value);
    std::string label(const BasicBlock* block) const;
    std::string reg(Type* type,int index) const;
    std::string allocated(const std::string& name,Type* type) const;
};
}
