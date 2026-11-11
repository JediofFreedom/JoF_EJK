// Memory-only image decoding. Callbacks own the returned RGBA8 pixels; codecs
// never access the engine filesystem, allocator, renderer state, or console.
#ifndef TR_IMAGE_DECODE_H
#define TR_IMAGE_DECODE_H
#include <cstddef>
struct ImageAllocator {
    void *(*allocate)(size_t);
    void (*release)(void *);
};
struct DecodedImage {
    unsigned char *pixels;
    int width, height;
};
enum class EncodedImageFormat { JPG, PNG };
struct EncodedImage {
    void *data;
    size_t length;
    EncodedImageFormat format;
    int width, height;
    char resolvedName[64];
};
// Main-thread filesystem read; caller releases data with FS_FreeFile.
bool R_ReadEncodedImage(const char *name, EncodedImage &image);

inline bool R_ProbeEncodedImage(const unsigned char *data, size_t length,
                               EncodedImageFormat format, int &width, int &height) {
    width = height = 0;
    if (!data) return false;
    if (format == EncodedImageFormat::PNG) {
        const unsigned char signature[] = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82};
        if (length < 33) return false;
        for (size_t i = 0; i < sizeof(signature); ++i) if (data[i] != signature[i]) return false;
        if (data[16] || data[17] || data[20] || data[21]) return false;
        width = int(data[18]) * 256 + data[19]; height = int(data[22]) * 256 + data[23];
    } else {
        if (length < 4 || data[0] != 255 || data[1] != 216) return false;
        for (size_t offset = 2; offset < length;) {
            if (data[offset++] != 255) return false;
            while (offset < length && data[offset] == 255) ++offset;
            if (offset == length) return false;
            const unsigned marker = data[offset++];
            if (marker == 217 || marker == 218 || marker == 0) return false;
            if (marker == 1 || (marker >= 208 && marker <= 215)) continue;
            if (length - offset < 2) return false;
            const size_t bytes = size_t(data[offset]) * 256 + data[offset + 1];
            if (bytes < 2 || bytes > length - offset) return false;
            if (marker == 192 || marker == 193 || marker == 194) {
                if (bytes < 8 || data[offset + 2] != 8) return false;
                height = int(data[offset + 3]) * 256 + data[offset + 4];
                width = int(data[offset + 5]) * 256 + data[offset + 6]; break;
            }
            offset += bytes;
        }
    }
    return width > 0 && height > 0 && !(width & (width - 1)) && !(height & (height - 1)) &&
        size_t(width) * height <= size_t(0x7fffffff) / 4;
}
bool R_DecodePNG(const void *source, size_t length, const ImageAllocator &allocator,
                 DecodedImage &image, char *error, size_t errorSize);
bool R_DecodeJPG(const void *source, size_t length, const ImageAllocator &allocator,
                 DecodedImage &image, char *error, size_t errorSize);
bool R_DecodeTGA(const void *source, size_t length, const ImageAllocator &allocator,
                 DecodedImage &image, char *error, size_t errorSize);
#endif
