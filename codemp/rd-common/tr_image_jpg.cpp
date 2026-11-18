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

/*
 * Include file for users of JPEG library.
 * You will need to have included system headers that define at least
 * the typedefs FILE and size_t before you can include jpeglib.h.
 * (stdio.h is sufficient on ANSI-conforming systems.)
 * You may also wish to include "jerror.h".
 */

#include <jpeglib.h>
#include <setjmp.h>
#include <jerror.h>
#include <climits>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include "tr_image_decode.h"

struct JPGReadError {
    jpeg_error_mgr pub;
    jmp_buf jump;
    char *message;
    size_t messageSize;
};

static void R_JPGReadErrorExit(j_common_ptr cinfo) {
    JPGReadError *error = reinterpret_cast<JPGReadError *>(cinfo->err);
    char message[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, message);
    if (error->message && error->messageSize)
        std::snprintf(error->message, error->messageSize, "%s", message);
    longjmp(error->jump, 1);
}
static void R_JPGOutputMessage(j_common_ptr) {}

struct JPGReadContext {
    jpeg_decompress_struct decoder;
    JPGReadError error;
    byte *pixels;
    ImageAllocator allocator;
};

bool R_DecodeJPG(const void *source, size_t length, const ImageAllocator &allocator,
                 DecodedImage &image, char *message, size_t messageSize) {
    image = {};
    if (message && messageSize) message[0] = 0;
    if (!source || !length || length > ULONG_MAX || !allocator.allocate || !allocator.release) return false;
    JPGReadContext *ctx = static_cast<JPGReadContext *>(std::calloc(1, sizeof(JPGReadContext)));
    if (!ctx) return false;
    ctx->allocator = allocator;
    ctx->error.message = message;
    ctx->error.messageSize = messageSize;
    ctx->decoder.err = jpeg_std_error(&ctx->error.pub);
    ctx->error.pub.error_exit = R_JPGReadErrorExit;
    ctx->error.pub.output_message = R_JPGOutputMessage;
    if (setjmp(ctx->error.jump)) {
        jpeg_destroy_decompress(&ctx->decoder);
        if (ctx->pixels) ctx->allocator.release(ctx->pixels);
        std::free(ctx);
        return false;
    }
    jpeg_create_decompress(&ctx->decoder);
    jpeg_mem_src(&ctx->decoder, static_cast<const byte *>(source), static_cast<unsigned long>(length));
    jpeg_read_header(&ctx->decoder, TRUE);
    if (!ctx->decoder.image_width || !ctx->decoder.image_height ||
        ctx->decoder.image_width > 65535 || ctx->decoder.image_height > 65535 ||
        size_t(ctx->decoder.image_width) * ctx->decoder.image_height > size_t(INT_MAX) / 4)
        ERREXIT(&ctx->decoder, JERR_WIDTH_OVERFLOW);
#ifdef JCS_ALPHA_EXTENSIONS
    ctx->decoder.out_color_space = JCS_EXT_RGBA;
#else
    ctx->decoder.out_color_space = JCS_RGB;
#endif
    jpeg_start_decompress(&ctx->decoder);
    const size_t count = size_t(ctx->decoder.output_width) * ctx->decoder.output_height;
    if (ctx->decoder.output_components != 3 && ctx->decoder.output_components != 4)
        ERREXIT(&ctx->decoder, JERR_CONVERSION_NOTIMPL);
    ctx->pixels = static_cast<byte *>(ctx->allocator.allocate(count * 4));
    if (!ctx->pixels) ERREXIT(&ctx->decoder, JERR_OUT_OF_MEMORY);
    const size_t stride = size_t(ctx->decoder.output_width) * ctx->decoder.output_components;
    while (ctx->decoder.output_scanline < ctx->decoder.output_height) {
        JSAMPROW row = ctx->pixels + stride * ctx->decoder.output_scanline;
        jpeg_read_scanlines(&ctx->decoder, &row, 1);
    }
    if (ctx->decoder.output_components == 3) {
        size_t input = count * 3, output = count * 4;
        do {
            ctx->pixels[--output] = 255;
            ctx->pixels[--output] = ctx->pixels[--input];
            ctx->pixels[--output] = ctx->pixels[--input];
            ctx->pixels[--output] = ctx->pixels[--input];
        } while (input);
    }
    jpeg_finish_decompress(&ctx->decoder);
    image = {ctx->pixels, static_cast<int>(ctx->decoder.output_width), static_cast<int>(ctx->decoder.output_height)};
    jpeg_destroy_decompress(&ctx->decoder);
    std::free(ctx);
    return true;
}

static void *JPG_Allocate(size_t size) { return Z_Malloc(static_cast<int>(size), TAG_TEMP_WORKSPACE, qfalse); }
void LoadJPG(const char *filename, byte **pic, int *width, int *height) {
    *pic = NULL;
    *width = *height = 0;
    void *source = NULL;
    const long length = ri.FS_ReadFile(filename, &source);
    if (!source) return;
    DecodedImage image = {};
    char message[JMSG_LENGTH_MAX] = {};
    const ImageAllocator allocator = {JPG_Allocate, ri.Z_Free};
    if (length > 0 && R_DecodeJPG(source, length, allocator, image, message, sizeof(message))) {
        *pic = image.pixels; *width = image.width; *height = image.height;
    } else if (message[0]) Com_Printf("LoadJPG: %s: %s\n", filename, message);
    ri.FS_FreeFile(source);
}

/* Expanded data destination object for stdio output */

typedef struct my_destination_mgr_s {
	struct jpeg_destination_mgr pub; /* public fields */

	byte* outfile;		/* target stream */
	int	size;
} my_destination_mgr;

typedef my_destination_mgr * my_dest_ptr;


/*
* Initialize destination --- called by jpeg_start_compress
* before any data is actually written.
*/

static void init_destination (j_compress_ptr cinfo)
{
	my_dest_ptr dest = (my_dest_ptr) cinfo->dest;

	dest->pub.next_output_byte = dest->outfile;
	dest->pub.free_in_buffer = dest->size;
}


/*
* Empty the output buffer --- called whenever buffer fills up.
*
* In typical applications, this should write the entire output buffer
* (ignoring the current state of next_output_byte & free_in_buffer),
* reset the pointer & count to the start of the buffer, and return TRUE
* indicating that the buffer has been dumped.
*
* In applications that need to be able to suspend compression due to output
* overrun, a FALSE return indicates that the buffer cannot be emptied now.
* In this situation, the compressor will return to its caller (possibly with
* an indication that it has not accepted all the supplied scanlines).  The
* application should resume compression after it has made more room in the
* output buffer.  Note that there are substantial restrictions on the use of
* suspension --- see the documentation.
*
* When suspending, the compressor will back up to a convenient restart point
* (typically the start of the current MCU). next_output_byte & free_in_buffer
* indicate where the restart point will be if the current call returns FALSE.
* Data beyond this point will be regenerated after resumption, so do not
* write it out when emptying the buffer externally.
*/

static boolean empty_output_buffer (j_compress_ptr cinfo)
{
	ERREXIT(cinfo, JERR_BUFFER_SIZE);

	return FALSE;
}

/*
* Terminate destination --- called by jpeg_finish_compress
* after all data has been written.  Usually needs to flush buffer.
*
* NB: *not* called by jpeg_abort or jpeg_destroy; surrounding
* application must deal with any cleanup that should happen even
* for error exit.
*/

static void term_destination(j_compress_ptr cinfo)
{
}


/*
* Prepare for output to a stdio stream.
* The caller must have already opened the stream, and is responsible
* for closing it after finishing compression.
*/

static void jpegDest (j_compress_ptr cinfo, byte* outfile, int size)
{
	my_dest_ptr dest;

	/* The destination object is made permanent so that multiple JPEG images
	* can be written to the same file without re-executing jpeg_stdio_dest.
	* This makes it dangerous to use this manager and a different destination
	* manager serially with the same JPEG object, because their private object
	* sizes may be different.  Caveat programmer.
	*/
	if (cinfo->dest == NULL) {	/* first time for this JPEG object? */
		cinfo->dest = (struct jpeg_destination_mgr *)
			(*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT,
			sizeof(my_destination_mgr));
	}

	dest = (my_dest_ptr) cinfo->dest;
	dest->pub.init_destination = init_destination;
	dest->pub.empty_output_buffer = empty_output_buffer;
	dest->pub.term_destination = term_destination;
	dest->outfile = outfile;
	dest->size = size;
}

/*
=================
SaveJPGToBuffer

Encodes JPEG from image in image_buffer and writes to buffer.
Expects RGB input data
=================
*/
size_t RE_SaveJPGToBuffer(byte *buffer, size_t bufSize, int quality,
	int image_width, int image_height, byte *image_buffer, int padding)
{
    if (!buffer || !image_buffer || !bufSize || bufSize > INT_MAX || padding < 0 ||
        image_width <= 0 || image_height <= 0 || image_width > JPEG_MAX_DIMENSION ||
        image_height > JPEG_MAX_DIMENSION || size_t(image_width) * 3 + padding > size_t(INT_MAX) / image_height) return 0;
    struct WriteContext { jpeg_compress_struct encoder; JPGReadError error; };
    WriteContext *ctx = static_cast<WriteContext *>(std::calloc(1, sizeof(WriteContext)));
    if (!ctx) return 0;
    ctx->encoder.err = jpeg_std_error(&ctx->error.pub);
    ctx->error.pub.error_exit = R_JPGReadErrorExit;
    ctx->error.pub.output_message = R_JPGOutputMessage;
    if (setjmp(ctx->error.jump)) {
        jpeg_destroy_compress(&ctx->encoder);
        std::free(ctx);
        return 0;
    }
    jpeg_create_compress(&ctx->encoder);
    jpegDest(&ctx->encoder, buffer, static_cast<int>(bufSize));
    ctx->encoder.image_width = image_width;
    ctx->encoder.image_height = image_height;
    ctx->encoder.input_components = 3;
    ctx->encoder.in_color_space = JCS_RGB;
    jpeg_set_defaults(&ctx->encoder);
    jpeg_set_quality(&ctx->encoder, quality, TRUE);
    if (quality >= 85) {
        ctx->encoder.comp_info[0].h_samp_factor = 1;
        ctx->encoder.comp_info[0].v_samp_factor = 1;
    }
    jpeg_start_compress(&ctx->encoder, TRUE);
    const size_t stride = size_t(image_width) * 3 + padding;
    while (ctx->encoder.next_scanline < ctx->encoder.image_height) {
        JSAMPROW row = image_buffer + (image_height - 1 - ctx->encoder.next_scanline) * stride;
        jpeg_write_scanlines(&ctx->encoder, &row, 1);
    }
    jpeg_finish_compress(&ctx->encoder);
    const my_dest_ptr dest = reinterpret_cast<my_dest_ptr>(ctx->encoder.dest);
    const size_t count = dest->size - dest->pub.free_in_buffer;
    jpeg_destroy_compress(&ctx->encoder);
    std::free(ctx);
    return count;
}


void RE_SaveJPG(const char * filename, int quality, int image_width, int image_height, byte *image_buffer, int padding)
{
    if (image_width <= 0 || image_height <= 0 ||
        size_t(image_width) * image_height > (size_t(INT_MAX) - 65536) / 3) return;
    // Small images still need room for JPEG headers and tables.
    const size_t capacity = size_t(image_width) * image_height * 3 + 65536;
    byte *out = static_cast<byte *>(Hunk_AllocateTempMemory(capacity));
    const size_t count = RE_SaveJPGToBuffer(out, capacity, quality, image_width, image_height, image_buffer, padding);
    if (count) ri.FS_WriteFile(filename, out, count);
    Hunk_FreeTempMemory(out);
}
