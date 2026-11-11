// Persistent pipeline cache for the native Vulkan backend.
#include "tr_local.h"
#include "vk_pipeline_cache_file.h"
#include <vector>

static void vk_pipeline_cache_path(char *path, size_t size,
                                   const VkPhysicalDeviceProperties &props) {
    // .dat is readable from the writable home even on pure servers. It is
    // renderer cache data and never participates in asset/pak checksums.
    Com_sprintf(path, size, "cache/vulkan/pipelines-%s-%08x-%08x-%08x.dat",
        ARCH_STRING, props.vendorID, props.deviceID, props.driverVersion);
}

void vk_create_pipeline_cache(void) {
    VkPipelineCacheCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    VkPhysicalDeviceProperties props;
    qvkGetPhysicalDeviceProperties(vk.physical_device, &props);
    std::vector<unsigned char> file;
    if (r_pipelineCache->integer) {
        char path[MAX_QPATH];
        vk_pipeline_cache_path(path, sizeof(path), props);
        fileHandle_t handle = 0;
        const long length = ri.FS_FOpenFileRead(path, &handle, qfalse);
        if (handle) {
            if (length >= long(PipelineCacheFile::HeaderSize + 32) &&
                length <= long(PipelineCacheFile::HeaderSize + PipelineCacheFile::MaxDataSize)) {
                file.resize(length);
                if (ri.FS_Read(file.data(), length, handle) == length &&
                    PipelineCacheFile::Validate(file.data(), file.size(), props)) {
                    ci.initialDataSize = file.size() - PipelineCacheFile::HeaderSize;
                    ci.pInitialData = file.data() + PipelineCacheFile::HeaderSize;
                }
            }
            ri.FS_FCloseFile(handle);
            if (!ci.initialDataSize)
                ri.Printf(PRINT_ALL, "Vulkan: ignoring incompatible or incomplete pipeline cache\n");
        }
    }
    vk.pipelineCache = VK_NULL_HANDLE;
    VkResult result = qvkCreatePipelineCache(vk.device, &ci, NULL, &vk.pipelineCache);
    if (result != VK_SUCCESS && ci.initialDataSize) {
        ri.Printf(PRINT_ALL, "Vulkan: cached pipeline creation failed (%d), retrying empty\n", result);
        ci.initialDataSize = 0;
        ci.pInitialData = NULL;
        vk.pipelineCache = VK_NULL_HANDLE;
        result = qvkCreatePipelineCache(vk.device, &ci, NULL, &vk.pipelineCache);
    }
    VK_CHECK(result);
    if (result != VK_SUCCESS) {
        // Cache creation is optional; failed Vulkan outputs are undefined.
        // Null explicitly so later graphics-pipeline calls run uncached.
        vk.pipelineCache = VK_NULL_HANDLE;
        ri.Printf(PRINT_ALL, "Vulkan: pipeline cache unavailable; continuing uncached\n");
        return;
    }
    ri.Printf(PRINT_ALL, "Vulkan: pipeline cache initialized with %u bytes\n", (unsigned)ci.initialDataSize);
}

void vk_save_pipeline_cache(void) {
    if (!r_pipelineCache->integer || vk.pipelineCache == VK_NULL_HANDLE) return;
    size_t size = 0;
    if (qvkGetPipelineCacheData(vk.device, vk.pipelineCache, &size, NULL) != VK_SUCCESS ||
        size < 32 || size > PipelineCacheFile::MaxDataSize) return;
    std::vector<unsigned char> file(PipelineCacheFile::HeaderSize + size);
    unsigned char *data = file.data() + PipelineCacheFile::HeaderSize;
    // VK_INCOMPLETE is not a complete cache and must never be persisted.
    if (qvkGetPipelineCacheData(vk.device, vk.pipelineCache, &size, data) != VK_SUCCESS ||
        size < 32 || size > file.size() - PipelineCacheFile::HeaderSize) return;
    file.resize(PipelineCacheFile::HeaderSize + size);
    VkPhysicalDeviceProperties props;
    qvkGetPhysicalDeviceProperties(vk.physical_device, &props);
    PipelineCacheFile::WriteHeader(file.data(), data, size, props);
    if (!PipelineCacheFile::Validate(file.data(), file.size(), props)) return;
    char path[MAX_QPATH];
    vk_pipeline_cache_path(path, sizeof(path), props);
    fileHandle_t handle = ri.FS_FOpenFileWrite(path, qfalse);
    if (!handle) return;
    const int written = ri.FS_Write(file.data(), static_cast<int>(file.size()), handle);
    ri.FS_FCloseFile(handle);
    if (written == static_cast<int>(file.size()))
        ri.Printf(PRINT_ALL, "Vulkan: saved %u bytes of pipeline cache\n", (unsigned)size);
}
