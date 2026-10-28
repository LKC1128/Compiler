#pragma once
#include "ir/function.hpp"
namespace sysy {
void assign_machine_locations(Function& fn,bool registers);
bool verify_machine_layout(const Function& fn,std::string& error);
}
