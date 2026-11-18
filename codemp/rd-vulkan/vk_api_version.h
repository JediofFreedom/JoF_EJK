// Negotiate the newest supported core API without requiring a newer loader.
#ifndef EJK_VK_API_VERSION_H
#define EJK_VK_API_VERSION_H
#include "vulkan/vulkan_core.h"

static uint32_t VK_SelectInstanceVersion(PFN_vkEnumerateInstanceVersion enumerateVersion) {
	uint32_t supported = VK_API_VERSION_1_0;
	if (!enumerateVersion || enumerateVersion(&supported) != VK_SUCCESS)
		return VK_API_VERSION_1_0;
	if (supported < VK_API_VERSION_1_0) return VK_API_VERSION_1_0;
	if (supported >= VK_API_VERSION_1_4) return VK_API_VERSION_1_4;
	// Application API versions use patch zero, even if the loader reports a patch.
	return VK_MAKE_API_VERSION(0, VK_API_VERSION_MAJOR(supported), VK_API_VERSION_MINOR(supported), 0);
}
#endif
