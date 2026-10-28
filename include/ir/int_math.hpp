#pragma once
#include "ir/bit_utils.hpp"
#include <cstdint>
#include <limits>
#include <optional>
namespace sysy {
inline int32_t int_add(int32_t a, int32_t b) { return bit_copy<int32_t>(uint32_t(a) + uint32_t(b)); }
inline int32_t int_sub(int32_t a, int32_t b) { return bit_copy<int32_t>(uint32_t(a) - uint32_t(b)); }
inline int32_t int_mul(int32_t a, int32_t b) { return bit_copy<int32_t>(uint32_t(a) * uint32_t(b)); }
inline bool int_div_defined(int32_t a, int32_t b) { return b != 0 && !(a == std::numeric_limits<int32_t>::min() && b == -1); }
}
