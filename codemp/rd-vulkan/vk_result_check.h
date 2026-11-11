#ifndef VK_RESULT_CHECK_H
#define VK_RESULT_CHECK_H
// Evaluate at the caller, before entering the helper's scope. The old macro's
// local `result` shadowed callers using VK_CHECK(result), reading itself instead.
static inline void VK_CheckResult(VkResult result, const char *expression) {
    if (result < 0)
        vk_debug("Vulkan: error %s returned by %s \n", vk_result_string(result), expression);
}
#define VK_CHECK(function_call) VK_CheckResult((function_call), #function_call)
#endif
