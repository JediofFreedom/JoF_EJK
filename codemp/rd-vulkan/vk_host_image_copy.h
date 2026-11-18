// EJK's Vulkan 1.4 path for populating new textures without staging/queue waits.
#ifndef EJK_VK_HOST_IMAGE_COPY_H
#define EJK_VK_HOST_IMAGE_COPY_H
#include <cstddef>
#include "vulkan/vulkan_core.h"

static bool VK_CanHostCopyImage(VkPhysicalDevice device, const VkImageCreateInfo &image,
	PFN_vkGetPhysicalDeviceImageFormatProperties2 query) {
	if (!query) return false;
	VkPhysicalDeviceImageFormatInfo2 request = {};
	request.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
	request.format = image.format; request.type = image.imageType;
	request.tiling = image.tiling; request.flags = image.flags;
	request.usage = image.usage | VK_IMAGE_USAGE_HOST_TRANSFER_BIT;
	VkHostImageCopyDevicePerformanceQuery performance = {};
	performance.sType = VK_STRUCTURE_TYPE_HOST_IMAGE_COPY_DEVICE_PERFORMANCE_QUERY;
	VkImageFormatProperties2 properties = {};
	properties.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
	properties.pNext = &performance;
	if (query(device, &request, &properties) != VK_SUCCESS || !performance.optimalDeviceAccess) return false;
	const auto &limits = properties.imageFormatProperties;
	return image.extent.width <= limits.maxExtent.width && image.extent.height <= limits.maxExtent.height &&
		image.extent.depth <= limits.maxExtent.depth && image.mipLevels <= limits.maxMipLevels &&
		image.arrayLayers <= limits.maxArrayLayers && (image.samples & limits.sampleCounts) != 0;
}

static VkResult VK_UploadHostImage(VkDevice device, VkImage image, uint32_t mipLevels,
	const VkBufferImageCopy *regions, uint32_t count, const void *pixels, size_t bytes,
	PFN_vkTransitionImageLayout transition, PFN_vkCopyMemoryToImage copy) {
	if (!transition || !copy || !pixels || !regions || !count || count > 16 || count != mipLevels)
		return VK_ERROR_INITIALIZATION_FAILED;
	VkMemoryToImageCopy host[16] = {};
	for (uint32_t i = 0; i < count; ++i) {
		if (regions[i].bufferOffset >= bytes) return VK_ERROR_INITIALIZATION_FAILED;
		host[i].sType = VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY;
		host[i].pHostPointer = static_cast<const unsigned char *>(pixels) + regions[i].bufferOffset;
		host[i].memoryRowLength = regions[i].bufferRowLength;
		host[i].memoryImageHeight = regions[i].bufferImageHeight;
		host[i].imageSubresource = regions[i].imageSubresource;
		host[i].imageOffset = regions[i].imageOffset;
		host[i].imageExtent = regions[i].imageExtent;
	}
	VkHostImageLayoutTransitionInfo layout = {};
	layout.sType = VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO;
	layout.image = image; layout.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	layout.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	layout.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	layout.subresourceRange.levelCount = mipLevels; layout.subresourceRange.layerCount = 1;
	VkResult result = transition(device, 1, &layout);
	if (result != VK_SUCCESS) return result;
	VkCopyMemoryToImageInfo upload = {};
	upload.sType = VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO;
	upload.dstImage = image; upload.dstImageLayout = layout.newLayout;
	upload.regionCount = count; upload.pRegions = host;
	return copy(device, &upload);
}
#endif
