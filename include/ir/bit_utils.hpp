#pragma once
#include <cstring>
#include <type_traits>

namespace sysy {
// C++17 object-representation copy; no pointer punning or inactive union reads.
template <class To, class From>
To bit_copy(const From& source) noexcept {
    static_assert(sizeof(To) == sizeof(From), "bit_copy requires equal sizes");
    static_assert(std::is_trivially_copyable<To>::value &&
                  std::is_trivially_copyable<From>::value,
                  "bit_copy requires trivially copyable types");
    To result;
    std::memcpy(&result, &source, sizeof(result));
    return result;
}
} // namespace sysy
