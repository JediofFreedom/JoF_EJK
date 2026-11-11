#include "vulkan/vulkan_core.h"
#include "vk_pipeline_cache_file.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#define CHECK(v) do { if (!(v)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #v); std::exit(1); } } while(0)

int main() {
    using namespace PipelineCacheFile;
    VkPhysicalDeviceProperties props = {};
    props.vendorID = 0x10de;
    props.deviceID = 0x2c05;
    props.driverVersion = 42;
    for (int i = 0; i < VK_UUID_SIZE; ++i) props.pipelineCacheUUID[i] = i + 1;
    std::vector<unsigned char> valid(HeaderSize + 64, 0xab);
    unsigned char *data = valid.data() + HeaderSize;
    Write32(data, 32);
    Write32(data + 4, VK_PIPELINE_CACHE_HEADER_VERSION_ONE);
    Write32(data + 8, props.vendorID);
    Write32(data + 12, props.deviceID);
    std::memcpy(data + 16, props.pipelineCacheUUID, VK_UUID_SIZE);
    WriteHeader(valid.data(), data, 64, props);
    CHECK(Validate(valid.data(), valid.size(), props));
    CHECK(!Validate(nullptr, 0, props));
    CHECK(!Validate(valid.data(), HeaderSize + MaxDataSize + 1, props));
    for (size_t size = 0; size < valid.size(); ++size)
        CHECK(!Validate(valid.data(), size, props));
    // Every metadata and payload byte is covered; partial writes and corrupt
    // opaque driver data are rejected before reaching Vulkan.
    for (size_t i = 0; i < valid.size(); ++i) {
        auto corrupt = valid;
        corrupt[i] ^= 1;
        CHECK(!Validate(corrupt.data(), corrupt.size(), props));
    }
    auto different = props;
    ++different.vendorID; CHECK(!Validate(valid.data(), valid.size(), different));
    different = props;
    ++different.deviceID; CHECK(!Validate(valid.data(), valid.size(), different));
    different = props;
    ++different.driverVersion; CHECK(!Validate(valid.data(), valid.size(), different));
    different = props;
    ++different.pipelineCacheUUID[15]; CHECK(!Validate(valid.data(), valid.size(), different));
    std::puts("Pipeline cache rejects truncated/corrupt data and incompatible GPUs/drivers.");
}
