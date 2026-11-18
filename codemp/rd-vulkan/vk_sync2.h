// EJK's feature-selected synchronization path; older devices stay native Vulkan.
#ifndef EJK_VK_SYNC2_H
#define EJK_VK_SYNC2_H
#include "vulkan/vulkan_core.h"

static VkResult VK_SubmitCommands(VkQueue queue, uint32_t count, const VkSubmitInfo *submits, VkFence fence,
	PFN_vkQueueSubmit legacy, PFN_vkQueueSubmit2 modern) {
	if (!modern || count != 1 || submits[0].pNext || submits[0].waitSemaphoreCount > 4 ||
		submits[0].signalSemaphoreCount > 4 || submits[0].commandBufferCount > 4)
		return legacy(queue, count, submits, fence);
	const VkSubmitInfo &source = submits[0];
	VkSemaphoreSubmitInfo waits[4] = {}, signals[4] = {};
	VkCommandBufferSubmitInfo commands[4] = {};
	for (uint32_t i = 0; i < source.waitSemaphoreCount; ++i) {
		waits[i].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		waits[i].semaphore = source.pWaitSemaphores[i];
		waits[i].stageMask = source.pWaitDstStageMask[i];
	}
	for (uint32_t i = 0; i < source.signalSemaphoreCount; ++i) {
		signals[i].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		signals[i].semaphore = source.pSignalSemaphores[i];
		signals[i].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	}
	for (uint32_t i = 0; i < source.commandBufferCount; ++i) {
		commands[i].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
		commands[i].commandBuffer = source.pCommandBuffers[i]; commands[i].deviceMask = 1;
	}
	VkSubmitInfo2 submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submit.waitSemaphoreInfoCount = source.waitSemaphoreCount; submit.pWaitSemaphoreInfos = waits;
	submit.signalSemaphoreInfoCount = source.signalSemaphoreCount; submit.pSignalSemaphoreInfos = signals;
	submit.commandBufferInfoCount = source.commandBufferCount; submit.pCommandBufferInfos = commands;
	return modern(queue, 1, &submit, fence);
}

static void VK_BufferBarrier(VkCommandBuffer commands, const VkBufferMemoryBarrier &buffer,
	PFN_vkCmdPipelineBarrier legacy, PFN_vkCmdPipelineBarrier2 modern) {
	if (!modern) {
		legacy(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
			0, 0, nullptr, 1, &buffer, 0, nullptr);
		return;
	}
	VkBufferMemoryBarrier2 barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
	barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
	barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
	barrier.srcAccessMask = buffer.srcAccessMask; barrier.dstAccessMask = buffer.dstAccessMask;
	barrier.srcQueueFamilyIndex = buffer.srcQueueFamilyIndex; barrier.dstQueueFamilyIndex = buffer.dstQueueFamilyIndex;
	barrier.buffer = buffer.buffer; barrier.offset = buffer.offset; barrier.size = buffer.size;
	VkDependencyInfo dependency = {};
	dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependency.bufferMemoryBarrierCount = 1; dependency.pBufferMemoryBarriers = &barrier;
	modern(commands, &dependency);
}

static void VK_ImageBarrier(VkCommandBuffer commands, VkPipelineStageFlags source, VkPipelineStageFlags destination,
	const VkImageMemoryBarrier &image, PFN_vkCmdPipelineBarrier legacy, PFN_vkCmdPipelineBarrier2 modern) {
	if (!modern) {
		legacy(commands, source, destination, 0, 0, nullptr, 0, nullptr, 1, &image);
		return;
	}
	VkImageMemoryBarrier2 barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	// Preserve legacy stage semantics without the deprecated top/bottom aliases.
	barrier.srcStageMask = source == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT ? VK_PIPELINE_STAGE_2_NONE :
		source == VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT ? VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT : VkPipelineStageFlags2(source);
	barrier.dstStageMask = destination == VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT ? VK_PIPELINE_STAGE_2_NONE :
		destination == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT ? VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT : VkPipelineStageFlags2(destination);
	barrier.srcAccessMask = image.srcAccessMask; barrier.dstAccessMask = image.dstAccessMask;
	barrier.oldLayout = image.oldLayout; barrier.newLayout = image.newLayout;
	barrier.srcQueueFamilyIndex = image.srcQueueFamilyIndex; barrier.dstQueueFamilyIndex = image.dstQueueFamilyIndex;
	barrier.image = image.image; barrier.subresourceRange = image.subresourceRange;
	VkDependencyInfo dependency = {};
	dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependency.imageMemoryBarrierCount = 1; dependency.pImageMemoryBarriers = &barrier;
	modern(commands, &dependency);
}
#endif
