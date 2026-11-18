#ifndef VK_TEXTURE_CONTEXT_H
#define VK_TEXTURE_CONTEXT_H

// Captured on the main thread when an image is requested. Even forced cvar
// changes during registration cannot change a queued image's processing or its
// cache key. GPU/engine work still runs on the main thread at commit.
struct TexturePreparationContext {
    int roundDown, picmip, nomip, textureBits, simpleMipMaps, colorMipLevels;
    int smartPicmip, maxTextureSize, compressedFormat, supportsGamma, fboActive;
    int mapLoading, mapGreyInteger;
    float mapGreyScale;
    unsigned char gamma[256], intensity[256];
};
#endif
