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
#include <png.h>
#include <cstdlib>
#include <climits>
#include <cstdio>
#include <cstring>

static void PNG_Write(png_structp png, png_bytep data, png_size_t length) {
    const fileHandle_t file = *static_cast<fileHandle_t *>(png_get_io_ptr(png));
    if (length > INT_MAX || ri.FS_Write(data, static_cast<int>(length), file) != static_cast<int>(length))
        png_error(png, "PNG write failed");
}

int RE_SavePNG(const char *filename, byte *buf, size_t width, size_t height, int byteDepth) {
    if (!buf || !width || !height || width > 65535 || height > 65535 ||
        (byteDepth != 3 && byteDepth != 4) || width * height > size_t(INT_MAX) / byteDepth) return -1;
    fileHandle_t file = ri.FS_FOpenFileWrite(filename, qtrue);
    if (!file) return -1;
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png ? png_create_info_struct(png) : NULL;
    if (!png || !info) {
        png_destroy_write_struct(&png, &info);
        ri.FS_FCloseFile(file);
        return -1;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        ri.FS_FCloseFile(file);
        return -1;
    }
    png_set_write_fn(png, &file, PNG_Write, NULL);
    png_set_IHDR(png, info, static_cast<png_uint_32>(width), static_cast<png_uint_32>(height),
        8, byteDepth == 4 ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB,
        PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    // Screenshots arrive bottom-up. Write their existing rows directly.
    for (size_t y = height; y; --y) png_write_row(png, buf + (y - 1) * width * byteDepth);
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    ri.FS_FCloseFile(file);
    return 0;
}

struct PNGReadContext {
    const byte *source;
    size_t length, offset;
    png_structp png;
    png_infop info;
    byte *pixels;
    png_bytep *rows;
    ImageAllocator allocator;
    char *error;
    size_t errorSize;
};

static void PNG_ReadError(png_structp png, png_const_charp message) {
    PNGReadContext *ctx = static_cast<PNGReadContext *>(png_get_error_ptr(png));
    if (ctx->error && ctx->errorSize) std::snprintf(ctx->error, ctx->errorSize, "%s", message);
    png_longjmp(png, 1);
}
static void PNG_ReadWarning(png_structp, png_const_charp) {}
static void PNG_Read(png_structp png, png_bytep data, png_size_t length) {
    PNGReadContext *ctx = static_cast<PNGReadContext *>(png_get_io_ptr(png));
    if (length > ctx->length - ctx->offset) png_error(png, "Truncated PNG");
    std::memcpy(data, ctx->source + ctx->offset, length);
    ctx->offset += length;
}

bool R_DecodePNG(const void *source, size_t length, const ImageAllocator &allocator,
                 DecodedImage &image, char *error, size_t errorSize) {
    image = {};
    if (error && errorSize) error[0] = 0;
    if (!source || length < 8 || png_sig_cmp(static_cast<png_const_bytep>(source), 0, 8) ||
        !allocator.allocate || !allocator.release) return false;
    // Mutable cleanup state lives on the heap: it remains defined after longjmp.
    PNGReadContext *ctx = static_cast<PNGReadContext *>(std::calloc(1, sizeof(PNGReadContext)));
    if (!ctx) return false;
    ctx->source = static_cast<const byte *>(source);
    ctx->length = length;
    ctx->allocator = allocator;
    ctx->error = error;
    ctx->errorSize = errorSize;
    ctx->png = png_create_read_struct(PNG_LIBPNG_VER_STRING, ctx, PNG_ReadError, PNG_ReadWarning);
    if (ctx->png) ctx->info = png_create_info_struct(ctx->png);
    if (!ctx->png || !ctx->info) {
        png_destroy_read_struct(&ctx->png, &ctx->info, NULL);
        std::free(ctx);
        return false;
    }
    if (setjmp(png_jmpbuf(ctx->png))) {
        if (ctx->pixels) ctx->allocator.release(ctx->pixels);
        std::free(ctx->rows);
        png_destroy_read_struct(&ctx->png, &ctx->info, NULL);
        std::free(ctx);
        return false;
    }
    png_set_read_fn(ctx->png, ctx, PNG_Read);
    png_set_user_limits(ctx->png, 65535, 65535);
#ifdef PNG_HANDLE_AS_UNKNOWN_SUPPORTED
    png_set_keep_unknown_chunks(ctx->png, PNG_HANDLE_CHUNK_NEVER, NULL, -1);
#endif
    png_read_info(ctx->png, ctx->info);
    const png_uint_32 width = png_get_image_width(ctx->png, ctx->info);
    const png_uint_32 height = png_get_image_height(ctx->png, ctx->info);
    const int depth = png_get_bit_depth(ctx->png, ctx->info);
    const int color = png_get_color_type(ctx->png, ctx->info);
    if (!width || !height || size_t(width) * height > size_t(INT_MAX) / 4)
        png_error(ctx->png, "PNG dimensions exceed renderer limits");
    if (depth == 16) png_set_strip_16(ctx->png);
    if (color == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(ctx->png);
    if (color == PNG_COLOR_TYPE_GRAY && depth < 8) png_set_expand_gray_1_2_4_to_8(ctx->png);
    const bool transparency = png_get_valid(ctx->png, ctx->info, PNG_INFO_tRNS) != 0;
    if (transparency) png_set_tRNS_to_alpha(ctx->png);
    if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(ctx->png);
    if (!(color & PNG_COLOR_MASK_ALPHA) && !transparency) png_set_add_alpha(ctx->png, 255, PNG_FILLER_AFTER);
    png_set_interlace_handling(ctx->png);
    png_read_update_info(ctx->png, ctx->info);
    // No gamma transform: preserve the established texture byte values.
    if (png_get_bit_depth(ctx->png, ctx->info) != 8 ||
        png_get_rowbytes(ctx->png, ctx->info) != size_t(width) * 4)
        png_error(ctx->png, "Unexpected PNG row format");
    ctx->pixels = static_cast<byte *>(ctx->allocator.allocate(size_t(width) * height * 4));
    ctx->rows = static_cast<png_bytep *>(std::malloc(size_t(height) * sizeof(png_bytep)));
    if (!ctx->pixels || !ctx->rows) png_error(ctx->png, "PNG allocation failed");
    for (size_t y = 0; y < height; ++y) ctx->rows[y] = ctx->pixels + y * width * 4;
    png_read_image(ctx->png, ctx->rows);
    png_read_end(ctx->png, NULL);
    image = {ctx->pixels, static_cast<int>(width), static_cast<int>(height)};
    std::free(ctx->rows);
    png_destroy_read_struct(&ctx->png, &ctx->info, NULL);
    std::free(ctx);
    return true;
}

static void *PNG_Allocate(size_t size) { return ri.Z_Malloc(static_cast<int>(size), TAG_TEMP_PNG, qfalse, 4); }
void LoadPNG(const char *filename, byte **data, int *width, int *height) {
    *data = NULL;
    *width = *height = 0;
    void *source = NULL;
    const long length = ri.FS_ReadFile(filename, &source);
    if (!source) return;
    DecodedImage image = {};
    char error[256] = {};
    const ImageAllocator allocator = {PNG_Allocate, ri.Z_Free};
    if (length > 0 && R_DecodePNG(source, length, allocator, image, error, sizeof(error))) {
        // Keep the legacy renderer's power-of-two policy.
        if ((image.width & (image.width - 1)) || (image.height & (image.height - 1))) {
            ri.Z_Free(image.pixels);
            ri.Printf(PRINT_ERROR, "LoadPNG: non-power-of-two image %s\n", filename);
        } else { *data = image.pixels; *width = image.width; *height = image.height; }
    } else if (error[0]) ri.Printf(PRINT_ERROR, "LoadPNG: %s: %s\n", filename, error);
    ri.FS_FreeFile(source);
}
