#pragma once
#include "ir/instruction.hpp"
namespace sysy {
// Clone first, remap every operand afterwards. No ownership is transferred.
std::unique_ptr<Instruction> clone_instruction(const Instruction& inst,TypeContext& ctx);
bool is_pure_expression(const Instruction& inst);
}
