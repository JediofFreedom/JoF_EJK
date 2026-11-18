// Persistent prepared-texture cache. The engine is only involved in resolving
// its writable directory; this module uses no engine or graphics entry points.
#ifndef VK_PREPARED_CACHE_H
#define VK_PREPARED_CACHE_H
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace PreparedTextures {
struct Key {
    uint64_t low, high;
    bool operator==(const Key &other) const { return low == other.low && high == other.high; }
};
enum class Format : uint32_t { RGBA8, RGBA4444, RGB5551, BC3 };
struct Description {
    uint32_t width, height, uploadWidth, uploadHeight, levels;
    Format format;
};
struct Texture { Description description = {}; std::vector<unsigned char> bytes; };
struct Statistics { uint64_t hits, misses, writes, droppedWrites, invalid; size_t queuedBytes; uint64_t diskBytes; };
static const size_t HeaderSize = 128;
static const size_t MaxPayload = 128 * 1024 * 1024;
Key ContentKey(const void *source, size_t length, const void *settings, size_t settingsLength);
std::string FileName(Key key);
size_t PayloadSize(const Description &description);
bool WriteHeader(unsigned char *header, const Description &description, Key key,
                 const unsigned char *data, size_t size);
bool ReadHeader(const unsigned char *header, size_t fileSize, Key expected,
                uint32_t width, uint32_t height, uint32_t allowedFormats, Description &description);
bool VerifyPayload(const unsigned char *header, const unsigned char *data, size_t size);

class Cache {
    struct State;
    std::unique_ptr<State> state;
public:
    Cache();
    ~Cache();
    // Configure/shutdown are main-thread calls made while decode workers idle.
    // The directory must already exist. A zero budget disables disk access.
    bool configure(const std::string &directory, size_t diskBudget, size_t queueBudget);
    bool read(Key key, uint32_t width, uint32_t height, uint32_t allowedFormats, Texture &texture);
    // Copy the reusable upload buffer into a bounded queue; never block loading
    // on disk writes. A full queue simply skips this best-effort cache entry.
    bool store(Key key, const Description &description, const void *data, size_t size);
    void shutdown();
    Statistics statistics() const;
};
}
#endif
