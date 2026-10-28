#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
namespace sysy {
class Value; class BasicBlock;
enum class IRLevel { HIR, MIR, LIR };
inline const char* level_name(IRLevel level) {
    return level == IRLevel::HIR ? "HIR" : level == IRLevel::MIR ? "MIR" : "LIR";
}
enum StateFlags : unsigned { SF_STRUCTURED=1, SF_SSA=2, SF_REGALLOC=4, SF_CANONICAL=8 };
enum UbFlags : unsigned { UB_NSW=1, UB_NUW=2, UB_EXACT=4, UB_INBOUNDS=8 };
// Structured source regions annotate the shared CFG. Array types and scoped
// declarations remain ordinary typed Alloca instructions inside these regions.
struct StructuredRegion {
    std::string kind;
    int parent=-1;
    BasicBlock* entry=nullptr;
    BasicBlock* exit=nullptr;
};
struct MachineLocation {
    std::string reg; // empty => explicit stack slot, relative to incoming sp/s0
    int offset=0;
    size_t bytes=8;
    bool object=false;
};
struct MachineLayout {
    std::unordered_map<const Value*,MachineLocation> values;
    std::vector<std::pair<std::string,int>> saved_registers;
    int frame_size=0;
    int outgoing_bytes=0;
};
} // namespace sysy
