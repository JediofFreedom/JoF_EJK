#ifndef VK_TEXTURE_SCRATCH_H
#define VK_TEXTURE_SCRATCH_H
#include <cstddef>
#include <vector>

// Private per-job storage, never backed by Zone/Hunk or shared renderer scratch.
// Exact-sized growth keeps its peak inside the reservation made before dispatch.
struct TexturePreparationBuffers {
    enum Slot { Upload, Pixels, Compressed, MipTemporary, ResampleIndices, Count };
    std::vector<unsigned char> buffers[Count];
    size_t limit, used = 0, peak = 0;
    explicit TexturePreparationBuffers(size_t budget) : limit(budget) {}
    unsigned char *allocate(Slot slot, size_t bytes) {
        auto &buffer = buffers[slot];
        if (buffer.size() >= bytes) return buffer.data();
        if (bytes > limit || used - buffer.size() > limit - bytes) return nullptr;
        used -= buffer.size();
        std::vector<unsigned char>().swap(buffer);
        try {
            buffer.resize(bytes);
            used += bytes;
            if (used > peak) peak = used;
        } catch (...) { return nullptr; }
        return buffer.data();
    }
};
#endif
