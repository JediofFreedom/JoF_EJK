namespace deviceFeatureTest {
struct cvar_t { int integer; } setting = {1};
static const int CVAR_ARCHIVE_ND = 1, CVAR_LATCH = 2;
static struct { uint32_t instanceApiVersion; } vk;
static struct {
    static cvar_t *Cvar_Get(const char *, const char *, int, const char *) { return &setting; }
    static void Cvar_CheckRange(cvar_t *, int low, int high, qboolean integral) { CHECK(low == 0 && high == 1 && integral); }
} ri;
static uint32_t deviceVersion;
static unsigned featureQueries;
static bool hasFeatures = true, sampledLayout = true;
static void qvkGetPhysicalDeviceProperties(VkPhysicalDevice, VkPhysicalDeviceProperties *properties) { properties->apiVersion = deviceVersion; }
static void VKAPI_CALL features(VkPhysicalDevice, VkPhysicalDeviceFeatures2 *properties) {
    ++featureQueries;
    auto *chain = static_cast<VkBaseOutStructure *>(properties->pNext);
    if (chain->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES)
        reinterpret_cast<VkPhysicalDeviceHostImageCopyFeatures *>(chain)->hostImageCopy = hasFeatures;
    else {
        CHECK(chain->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES);
        reinterpret_cast<VkPhysicalDeviceSynchronization2Features *>(chain)->synchronization2 = hasFeatures;
    }
}
static void VKAPI_CALL properties(VkPhysicalDevice, VkPhysicalDeviceProperties2 *properties) {
    auto *copy = static_cast<VkPhysicalDeviceHostImageCopyProperties *>(properties->pNext);
    CHECK(copy->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_PROPERTIES);
    if (copy->pCopyDstLayouts) { CHECK(copy->copyDstLayoutCount == 1); copy->pCopyDstLayouts[0] = sampledLayout ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL; }
    copy->copyDstLayoutCount = 1;
}
static PFN_vkGetPhysicalDeviceFeatures2 qvkGetPhysicalDeviceFeatures2 = features;
static PFN_vkGetPhysicalDeviceProperties2 qvkGetPhysicalDeviceProperties2 = properties;
static PFN_vkGetPhysicalDeviceImageFormatProperties2 qvkGetPhysicalDeviceImageFormatProperties2 = hostCopyCapabilityTest::query;
#include "device_features.h"
static void check() {
    VkPhysicalDeviceHostImageCopyFeatures host = {};
    host.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES;
    VkPhysicalDeviceSynchronization2Features synchronization = {};
    synchronization.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    vk.instanceApiVersion = deviceVersion = VK_API_VERSION_1_4;
    setting.integer = 0;
    CHECK(!vk_prepare_host_image_copy(VK_NULL_HANDLE, &host) && !vk_prepare_synchronization2(VK_NULL_HANDLE, &synchronization));
    CHECK(featureQueries == 0); setting.integer = 1;
    vk.instanceApiVersion = VK_API_VERSION_1_0;
    CHECK(!vk_prepare_host_image_copy(VK_NULL_HANDLE, &host) && !vk_prepare_synchronization2(VK_NULL_HANDLE, &synchronization));
    CHECK(featureQueries == 0); vk.instanceApiVersion = VK_API_VERSION_1_4;
    deviceVersion = VK_API_VERSION_1_2;
    CHECK(!vk_prepare_host_image_copy(VK_NULL_HANDLE, &host) && !vk_prepare_synchronization2(VK_NULL_HANDLE, &synchronization));
    CHECK(featureQueries == 0); deviceVersion = VK_API_VERSION_1_3;
    CHECK(!vk_prepare_host_image_copy(VK_NULL_HANDLE, &host) && vk_prepare_synchronization2(VK_NULL_HANDLE, &synchronization));
    deviceVersion = VK_API_VERSION_1_4;
    CHECK(vk_prepare_host_image_copy(VK_NULL_HANDLE, &host) && vk_prepare_synchronization2(VK_NULL_HANDLE, &synchronization));
    sampledLayout = false; CHECK(!vk_prepare_host_image_copy(VK_NULL_HANDLE, &host)); sampledLayout = true;
    hasFeatures = false;
    CHECK(!vk_prepare_host_image_copy(VK_NULL_HANDLE, &host) && !vk_prepare_synchronization2(VK_NULL_HANDLE, &synchronization));
    hasFeatures = true; qvkGetPhysicalDeviceFeatures2 = nullptr;
    CHECK(!vk_prepare_host_image_copy(VK_NULL_HANDLE, &host) && !vk_prepare_synchronization2(VK_NULL_HANDLE, &synchronization));
    qvkGetPhysicalDeviceFeatures2 = features;
}
}

namespace pipelineIndexTest {
using cullType_t = int;
using Vk_Shader_Type = int;
using Vk_Shadow_Phase = int;
using Vk_Primitive_Topology = int;
#define USE_VBO
#include "pipeline_types.h"
#include "pipeline_key.h"
#undef USE_VBO
static void check() {
    Vk_Pipeline_Def definition = {}, changed = {};
    const VkPipelineKey key = vk_pipeline_key(definition);
    // The real production struct has tail padding which must not affect reuse.
    static_assert(offsetof(Vk_Pipeline_Def, color) + sizeof(definition.color) < sizeof(definition), "Expected padding fixture");
    reinterpret_cast<byte *>(&changed)[sizeof(changed) - 1] = 0xa5;
    CHECK(std::memcmp(&definition, &changed, sizeof(definition)) && vk_pipeline_key(changed) == key);
#define DIFFERENT(member) changed = definition; ++changed.member; CHECK(vk_pipeline_key(changed) != key)
    DIFFERENT(state_bits); DIFFERENT(face_culling); DIFFERENT(polygon_offset); DIFFERENT(mirror);
    DIFFERENT(vbo_ghoul2); DIFFERENT(vbo_mdv); DIFFERENT(shader_type); DIFFERENT(shadow_phase);
    DIFFERENT(primitives); DIFFERENT(surface_sprite_flags); DIFFERENT(line_width); DIFFERENT(fog_stage);
    DIFFERENT(abs_light); DIFFERENT(allow_discard); DIFFERENT(acff); DIFFERENT(color.rgb); DIFFERENT(color.alpha);
#undef DIFFERENT
    VkPipelineIndex lookup;
    uint32_t index = 99;
    CHECK(!lookup.find(key, 0, index));
    lookup.add(key, 2); lookup.add(key, 8);
    CHECK(lookup.find(key, 0, index) && index == 2);
    CHECK(lookup.find(key, 3, index) && index == 8);
    CHECK(!lookup.find(key, 9, index));
    for (unsigned i = 1; i < 2000; ++i) {
        definition.state_bits = i; lookup.add(vk_pipeline_key(definition), i + 10);
    }
    for (unsigned i = 1; i < 2000; ++i) {
        definition.state_bits = i;
        CHECK(lookup.find(vk_pipeline_key(definition), 0, index) && index == i + 10);
    }
    lookup.clear(); CHECK(!lookup.find(key, 0, index));
    lookup.add(key, 0); CHECK(lookup.find(key, 0, index) && index == 0);
}
}

namespace synchronization2Test {
static unsigned legacySubmits, modernSubmits, legacyBarriers, modernBarriers;
static const VkFence expectedFence = (VkFence)(uintptr_t)4;
static VkImageMemoryBarrier2 captured;
static VkResult VKAPI_CALL legacySubmit(VkQueue, uint32_t, const VkSubmitInfo *, VkFence fence) {
    CHECK(fence == expectedFence); ++legacySubmits; return VK_SUCCESS;
}
static VkResult VKAPI_CALL modernSubmit(VkQueue, uint32_t count, const VkSubmitInfo2 *submit, VkFence fence) {
    CHECK(count == 1 && fence == expectedFence && submit->pNext == nullptr);
    CHECK(submit->waitSemaphoreInfoCount == 2 && submit->signalSemaphoreInfoCount == 1);
    CHECK(submit->pWaitSemaphoreInfos[0].stageMask == VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    CHECK(submit->pWaitSemaphoreInfos[1].stageMask == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    CHECK(submit->pWaitSemaphoreInfos[0].semaphore == (VkSemaphore)(uintptr_t)1);
    CHECK(submit->pWaitSemaphoreInfos[1].semaphore == (VkSemaphore)(uintptr_t)2);
    CHECK(submit->pWaitSemaphoreInfos[0].value == 0 && submit->pSignalSemaphoreInfos[0].value == 0);
    CHECK(submit->pSignalSemaphoreInfos[0].stageMask == VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
    CHECK(submit->commandBufferInfoCount == 1 && submit->pCommandBufferInfos[0].deviceMask == 1);
    CHECK(submit->pCommandBufferInfos[0].commandBuffer == (VkCommandBuffer)(uintptr_t)3);
    ++modernSubmits; return VK_SUCCESS;
}
static void VKAPI_CALL legacyBarrier(VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags,
    VkDependencyFlags, uint32_t, const VkMemoryBarrier *, uint32_t, const VkBufferMemoryBarrier *,
    uint32_t count, const VkImageMemoryBarrier *image) {
    CHECK(count == 1 && image->oldLayout == VK_IMAGE_LAYOUT_UNDEFINED); ++legacyBarriers;
}
static void VKAPI_CALL modernBarrier(VkCommandBuffer, const VkDependencyInfo *dependency) {
    CHECK(dependency->imageMemoryBarrierCount == 1 && dependency->bufferMemoryBarrierCount == 0);
    captured = dependency->pImageMemoryBarriers[0]; ++modernBarriers;
}
static void check() {
    VkSemaphore waits[2] = {(VkSemaphore)(uintptr_t)1, (VkSemaphore)(uintptr_t)2};
    VkPipelineStageFlags stages[2] = {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkCommandBuffer commands = (VkCommandBuffer)(uintptr_t)3;
    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; submit.waitSemaphoreCount = 2;
    submit.pWaitSemaphores = waits; submit.pWaitDstStageMask = stages;
    submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = waits;
    submit.commandBufferCount = 1; submit.pCommandBuffers = &commands;
    CHECK(VK_SubmitCommands(VK_NULL_HANDLE, 1, &submit, expectedFence, legacySubmit, modernSubmit) == VK_SUCCESS);
    CHECK(modernSubmits == 1 && legacySubmits == 0);
    CHECK(VK_SubmitCommands(VK_NULL_HANDLE, 1, &submit, expectedFence, legacySubmit, nullptr) == VK_SUCCESS);
    VkTimelineSemaphoreSubmitInfo timeline = {};
    timeline.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    submit.pNext = &timeline;
    CHECK(VK_SubmitCommands(VK_NULL_HANDLE, 1, &submit, expectedFence, legacySubmit, modernSubmit) == VK_SUCCESS);
    CHECK(legacySubmits == 2 && modernSubmits == 1); // Preserve unsupported extension chains.
    VkImageMemoryBarrier image = {};
    image.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; image.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    image.srcQueueFamilyIndex = image.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    image.image = (VkImage)(uintptr_t)5; image.subresourceRange.levelCount = 4;
    image.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; image.subresourceRange.layerCount = 1;
    VK_ImageBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, image, legacyBarrier, modernBarrier);
    CHECK(captured.srcStageMask == VK_PIPELINE_STAGE_2_NONE && captured.dstStageMask == VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    CHECK(captured.dstAccessMask == VK_ACCESS_2_TRANSFER_WRITE_BIT && captured.image == image.image);
    CHECK(captured.subresourceRange.levelCount == 4 && captured.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
    VK_ImageBarrier(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, image, legacyBarrier, modernBarrier);
    CHECK(captured.srcStageMask == VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT && captured.dstStageMask == VK_PIPELINE_STAGE_2_NONE);
    VK_ImageBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, image, legacyBarrier, nullptr);
    CHECK(modernBarriers == 2 && legacyBarriers == 1);
}
}
