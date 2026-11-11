// Persistent Vulkan pipeline cache format. See UPSTREAM.md and ../../LICENSE.txt.
#ifndef VK_PIPELINE_CACHE_FILE_H
#define VK_PIPELINE_CACHE_FILE_H

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace PipelineCacheFile {
static const size_t HeaderSize = 24;
static const size_t MaxDataSize = 64 * 1024 * 1024;

inline uint32_t Read32(const unsigned char *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
        (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline void Write32(unsigned char *p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(v >> (8 * i));
}
inline uint64_t Checksum(const unsigned char *p, size_t size) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < size; ++i) hash = (hash ^ p[i]) * UINT64_C(1099511628211);
    return hash;
}
inline void WriteHeader(unsigned char *header, const unsigned char *data,
                        size_t size, const VkPhysicalDeviceProperties &props) {
    std::memcpy(header, "EVK1", 4);
    Write32(header + 4, static_cast<uint32_t>(size));
    Write32(header + 8, props.driverVersion);
    Write32(header + 12, sizeof(void *));
    const uint64_t hash = Checksum(data, size);
    Write32(header + 16, static_cast<uint32_t>(hash));
    Write32(header + 20, static_cast<uint32_t>(hash >> 32));
}
inline bool Validate(const unsigned char *file, size_t size,
                     const VkPhysicalDeviceProperties &props) {
    // Decode bytes explicitly: Vulkan's 32-byte v1 header is little endian,
    // independent of host byte order and C struct packing.
    if (!file || size < HeaderSize + 32 || size > HeaderSize + MaxDataSize)
        return false;
    if (std::memcmp(file, "EVK1", 4) || Read32(file + 4) != size - HeaderSize ||
        Read32(file + 8) != props.driverVersion || Read32(file + 12) != sizeof(void *))
        return false;
    const unsigned char *data = file + HeaderSize;
    if (Read32(data) != 32 || Read32(data + 4) != VK_PIPELINE_CACHE_HEADER_VERSION_ONE ||
        Read32(data + 8) != props.vendorID || Read32(data + 12) != props.deviceID ||
        std::memcmp(data + 16, props.pipelineCacheUUID, VK_UUID_SIZE))
        return false;
    const uint64_t hash = uint64_t(Read32(file + 16)) | (uint64_t(Read32(file + 20)) << 32);
    return hash == Checksum(data, size - HeaderSize);
}
}
#endif
