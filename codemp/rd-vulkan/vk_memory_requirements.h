#ifndef VK_MEMORY_REQUIREMENTS_H
#define VK_MEMORY_REQUIREMENTS_H

// Requirements2 is available only when the device enabled the corresponding
// extension pair. Mandatory dedicated allocations must never enter an arena.
static bool vk_image_memory_requirements(VkImage image, VkMemoryRequirements *requirements) {
    if (!vk.dedicatedAllocation) {
        qvkGetImageMemoryRequirements(vk.device, image, requirements);
        return false;
    }
    VkMemoryDedicatedRequirementsKHR dedicated = {};
    dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR;
    VkMemoryRequirements2KHR output = {};
    output.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR;
    output.pNext = &dedicated;
    VkImageMemoryRequirementsInfo2KHR input = {};
    input.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2_KHR;
    input.image = image;
    qvkGetImageMemoryRequirements2KHR(vk.device, &input, &output);
    *requirements = output.memoryRequirements;
    return dedicated.requiresDedicatedAllocation != VK_FALSE;
}

static bool vk_buffer_memory_requirements(VkBuffer buffer, VkMemoryRequirements *requirements) {
    if (!vk.dedicatedAllocation) {
        qvkGetBufferMemoryRequirements(vk.device, buffer, requirements);
        return false;
    }
    VkMemoryDedicatedRequirementsKHR dedicated = {};
    dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR;
    VkMemoryRequirements2KHR output = {};
    output.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR;
    output.pNext = &dedicated;
    VkBufferMemoryRequirementsInfo2KHR input = {};
    input.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2_KHR;
    input.buffer = buffer;
    qvkGetBufferMemoryRequirements2KHR(vk.device, &input, &output);
    *requirements = output.memoryRequirements;
    return dedicated.requiresDedicatedAllocation != VK_FALSE;
}

static void vk_allocate_buffer_memory(VkBuffer buffer, VkMemoryPropertyFlags properties,
    VkDeviceMemory *memory, const char *name) {
    VkMemoryRequirements requirements;
    const bool dedicated = vk_buffer_memory_requirements(buffer, &requirements);
    VkMemoryDedicatedAllocateInfoKHR dedicatedInfo = {};
    dedicatedInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
    dedicatedInfo.buffer = buffer;
    VkMemoryAllocateInfo allocation = {};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.pNext = dedicated ? &dedicatedInfo : NULL;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = vk_find_memory_type(requirements.memoryTypeBits, properties);
    VK_ALLOCATE_MEMORY_CHECK(vk.device, &allocation, memory, name);
}
#endif
