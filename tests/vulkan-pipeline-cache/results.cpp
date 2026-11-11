#include "vulkan/vulkan_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#define CHECK(v) do { if (!(v)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #v); std::exit(1); } } while (0)
static unsigned errors, calls;
static char last[256];
static const char *vk_result_string(VkResult value) { CHECK(value == VK_ERROR_DEVICE_LOST); return "device lost"; }
static void vk_debug(const char *format, ...) {
    ++errors; va_list args; va_start(args, format); std::vsnprintf(last, sizeof(last), format, args); va_end(args);
}
#include "vk_result_check.h"
static VkResult operation() { ++calls; return VK_ERROR_DEVICE_LOST; }
int main() {
    VkResult result = VK_SUCCESS;
    VK_CHECK(result); CHECK(errors == 0);
    result = VK_ERROR_DEVICE_LOST;
    VK_CHECK(result); CHECK(errors == 1 && std::strstr(last, "result"));
    VK_CHECK(operation()); CHECK(errors == 2 && calls == 1 && std::strstr(last, "operation()"));
    if (true) VK_CHECK(VK_SUCCESS); else std::abort();
    VK_CHECK(VK_INCOMPLETE); CHECK(errors == 2);
}
