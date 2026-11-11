// Select only sample counts supported by every framebuffer attachment.
#ifndef EJK_VK_SAMPLE_COUNTS_H
#define EJK_VK_SAMPLE_COUNTS_H
#include "vulkan/vulkan_core.h"

static VkSampleCountFlagBits VK_SelectSampleCount(VkSampleCountFlags supported, uint32_t requested) {
    for (uint32_t count = VK_SAMPLE_COUNT_64_BIT; count > VK_SAMPLE_COUNT_1_BIT; count >>= 1)
        if (count <= requested && (supported & count))
            return static_cast<VkSampleCountFlagBits>(count);
    return VK_SAMPLE_COUNT_1_BIT;
}

static VkSampleCountFlags VK_FramebufferSampleCounts(const VkPhysicalDeviceLimits &limits, bool stencil) {
    VkSampleCountFlags supported = limits.framebufferColorSampleCounts & limits.framebufferDepthSampleCounts;
    if (stencil) supported &= limits.framebufferStencilSampleCounts;
    return supported;
}
#endif
