#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <type_traits>
#include "buffer-offset.h"

static void require(bool value, const char *message) {
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
int main() {
    static_assert(std::is_same<decltype(BUFFER_OFFSET(0)),char *>::value,
        "Vulkan buffer offset must preserve its char pointer interface");
    volatile uintptr_t input=0;
    require(reinterpret_cast<uintptr_t>(BUFFER_OFFSET(input))==0,"zero offset must preserve zero bits");
    input=24;
    require(reinterpret_cast<uintptr_t>(BUFFER_OFFSET(input))==24,"nonzero offset must preserve bits");
    input=std::numeric_limits<uintptr_t>::max()-15;
    require(reinterpret_cast<uintptr_t>(BUFFER_OFFSET(input))==input,"offset must preserve full uintptr_t width");
    uintptr_t once=7;
    char *encoded=BUFFER_OFFSET(once++);
    require(reinterpret_cast<uintptr_t>(encoded)==7 && once==8,"offset expression must be evaluated once");
    return 0;
}
