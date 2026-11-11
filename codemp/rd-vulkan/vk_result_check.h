#ifndef VK_RESULT_CHECK_H
#define VK_RESULT_CHECK_H
// Evaluate at the caller, before entering the helper's scope. The old macro's
// local `result` shadowed callers using VK_CHECK(result), reading itself instead.
static inline void VK_CheckResult(VkResult result, const char *expression) {
    if (result < 0)
        vk_debug("Vulkan: error %s returned by %s \n", vk_result_string(result), expression);
}
#define VK_CHECK(function_call) VK_CheckResult((function_call), #function_call)
// Creation/binding/reset operations cannot proceed with invalid output handles.
// Keep this separate from logging-only VK_CHECK, whose callers can intentionally
// handle recoverable Vulkan status codes themselves. Evaluate exactly once.
#define VK_REQUIRE_SUCCESS(function_call) do { \
    const VkResult vkRequiredResult_ = (function_call); \
    if (vkRequiredResult_ != VK_SUCCESS) \
        ri.Error(ERR_FATAL, "Vulkan: %s failed: %s", #function_call, vk_result_string(vkRequiredResult_)); \
} while (0)
#endif
