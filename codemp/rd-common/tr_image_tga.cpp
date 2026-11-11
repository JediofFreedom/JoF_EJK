/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2005 - 2015, ioquake3 contributors
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

#include "tr_common.h"
#include "tr_image_decode.h"
#include <climits>
#include <cstdio>
#include <cstring>

bool R_DecodeTGA(const void *source, size_t length, const ImageAllocator &allocator,
                 DecodedImage &image, char *error, size_t errorSize) {
    image = {};
    if (error && errorSize) error[0] = 0;
    byte *pixels = NULL;
    const auto fail = [&](const char *message) {
        if (pixels) allocator.release(pixels);
        if (error && errorSize) std::snprintf(error, errorSize, "%s", message);
        return false;
    };
    if (!source || length < 18 || !allocator.allocate || !allocator.release) return fail("Truncated TGA header");
    const byte *data = static_cast<const byte *>(source);
    const unsigned type = data[2], bits = data[16], descriptor = data[17];
    const unsigned width = unsigned(data[12]) | (unsigned(data[13]) << 8);
    const unsigned height = unsigned(data[14]) | (unsigned(data[15]) << 8);
    if (data[1] || data[3] || data[4]) return fail("Color-mapped TGA is unsupported");
    if ((type != 2 && type != 3 && type != 10) ||
        (bits != 24 && bits != 32 && !(bits == 8 && type == 3)) || (descriptor & 0xc0))
        return fail("Unsupported TGA pixel format or interleaving");
    if (!width || !height || size_t(width) * height > size_t(INT_MAX) / 4)
        return fail("Invalid TGA dimensions");
    size_t offset = 18 + size_t(data[0]);
    if (offset > length) return fail("Truncated TGA image ID");
    const unsigned channels = bits / 8;
    const size_t count = size_t(width) * height;
    if (type != 10 && count > (length - offset) / channels) return fail("Truncated TGA pixels");
    pixels = static_cast<byte *>(allocator.allocate(count * 4));
    if (!pixels) return fail("TGA allocation failed");
    unsigned x = 0, y = 0;
    size_t written = 0;
    while (written < count) {
        size_t packet = count - written;
        bool repeat = false;
        if (type == 10) {
            if (offset == length) return fail("Truncated TGA packet header");
            const unsigned header = data[offset++];
            packet = (header & 127) + 1; repeat = (header & 128) != 0;
            if (packet > count - written) return fail("TGA packet exceeds image dimensions");
        }
        const size_t bytes = (repeat ? 1 : packet) * channels;
        if (bytes > length - offset) return fail("Truncated TGA packet pixels");
        const byte *input = data + offset;
        for (size_t i = 0; i < packet; ++i) {
            const unsigned targetX = (descriptor & 16) ? width - 1 - x : x;
            const unsigned targetY = (descriptor & 32) ? y : height - 1 - y;
            byte *out = pixels + (size_t(targetY) * width + targetX) * 4;
            out[0] = channels == 1 ? input[0] : input[2];
            out[1] = channels == 1 ? input[0] : input[1];
            out[2] = input[0]; out[3] = channels == 4 ? input[3] : 255;
            if (!repeat) input += channels;
            if (++x == width) { x = 0; ++y; }
        }
        written += packet; offset += bytes;
    }
    image = {pixels, static_cast<int>(width), static_cast<int>(height)};
    return true;
}

static void *TGA_Allocate(size_t size) { return Z_Malloc(static_cast<int>(size), TAG_TEMP_WORKSPACE, qfalse); }
void LoadTGA(const char *filename, byte **pic, int *width, int *height) {
    *pic = NULL;
    if (width) *width = 0;
    if (height) *height = 0;
    void *source = NULL;
    const long length = ri.FS_ReadFile(filename, &source);
    if (!source) return;
    const ImageAllocator allocator = {TGA_Allocate, ri.Z_Free};
    DecodedImage image = {};
    char error[256] = {};
    if (length > 0 && R_DecodeTGA(source, length, allocator, image, error, sizeof(error))) {
        *pic = image.pixels;
        if (width) *width = image.width;
        if (height) *height = image.height;
    } else if (error[0]) ri.Printf(PRINT_ERROR, "LoadTGA: %s: %s\n", filename, error);
    ri.FS_FreeFile(source);
}
