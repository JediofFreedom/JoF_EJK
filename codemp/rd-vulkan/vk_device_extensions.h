#ifndef VK_DEVICE_EXTENSIONS_H
#define VK_DEVICE_EXTENSIONS_H
#include <cstring>
#include <vector>

// The printable extension string is truncated to MAX_STRING_CHARS. Feature
// detection must query complete names, including extensions beyond that prefix.
template<class Enumerate>
bool VK_HasDeviceExtension(VkPhysicalDevice device, const char *name, Enumerate enumerate) {
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        uint32_t count = 0;
        if (enumerate(device, nullptr, &count, nullptr) != VK_SUCCESS || !count || count > 65536) return false;
        std::vector<VkExtensionProperties> extensions(count);
        const VkResult result = enumerate(device, nullptr, &count, extensions.data());
        if (result == VK_INCOMPLETE) continue;
        if (result != VK_SUCCESS || count > extensions.size()) return false;
        for (uint32_t i = 0; i < count; ++i)
            if (!std::strcmp(extensions[i].extensionName, name)) return true;
        return false;
    }
    return false;
}
#endif
