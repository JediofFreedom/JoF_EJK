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

static void R_JPGErrorExit(j_common_ptr cinfo)
{
	char buffer[JMSG_LENGTH_MAX];

	(*cinfo->err->format_message) (cinfo, buffer);

	/* Let the memory manager delete any temp files before we die */
	jpeg_destroy(cinfo);

	Com_Printf("%s", buffer);
}

static void R_JPGOutputMessage(j_common_ptr cinfo)
{
	char buffer[JMSG_LENGTH_MAX];

	/* Create the message */
	(*cinfo->err->format_message) (cinfo, buffer);

	/* Send it to stderr, adding a newline */
	Com_Printf("%s\n", buffer);
}

struct JPGReadError {
	jpeg_error_mgr pub;
	jmp_buf jump;
};

static void R_JPGReadErrorExit(j_common_ptr cinfo) {
	R_JPGOutputMessage(cinfo);
	longjmp(reinterpret_cast<JPGReadError *>(cinfo->err)->jump, 1);
}

void LoadJPG(const char *filename, byte **pic, int *width, int *height) {
	*pic = NULL;
	*width = *height = 0;
	void *source = NULL;
	const long length = ri.FS_ReadFile(filename, &source);
	if (!source) return;
	if (length <= 0) { ri.FS_FreeFile(source); return; }
	jpeg_decompress_struct cinfo = {};
	JPGReadError error;
	byte *volatile pixels = NULL;
	cinfo.err = jpeg_std_error(&error.pub);
	error.pub.error_exit = R_JPGReadErrorExit;
	error.pub.output_message = R_JPGOutputMessage;
	if (setjmp(error.jump)) {
		jpeg_destroy_decompress(&cinfo);
		if (pixels) ri.Z_Free(pixels);
		ri.FS_FreeFile(source);
		return;
	}
	jpeg_create_decompress(&cinfo);
	jpeg_mem_src(&cinfo, static_cast<byte *>(source), length);
	jpeg_read_header(&cinfo, TRUE);
#ifdef JCS_ALPHA_EXTENSIONS
	// libjpeg-turbo writes opaque RGBA directly, including SIMD color conversion.
	cinfo.out_color_space = JCS_EXT_RGBA;
#else
	cinfo.out_color_space = JCS_RGB;
#endif
	jpeg_start_decompress(&cinfo);
	const size_t count = size_t(cinfo.output_width) * cinfo.output_height;
	if (!cinfo.output_width || !cinfo.output_height || count > 0x1fffffff ||
		count / cinfo.output_width != cinfo.output_height ||
		(cinfo.output_components != 3 && cinfo.output_components != 4)) {
		jpeg_destroy_decompress(&cinfo);
		ri.FS_FreeFile(source);
		Com_Printf("LoadJPG: invalid dimensions or components in %s\n", filename);
		return;
	}
	pixels = static_cast<byte *>(Z_Malloc(count * 4, TAG_TEMP_WORKSPACE, qfalse));
	const size_t stride = size_t(cinfo.output_width) * cinfo.output_components;
	while (cinfo.output_scanline < cinfo.output_height) {
		JSAMPROW row = pixels + stride * cinfo.output_scanline;
		jpeg_read_scanlines(&cinfo, &row, 1);
	}
	if (cinfo.output_components == 3) {
		size_t input = count * 3, output = count * 4;
		do {
			pixels[--output] = 255;
			pixels[--output] = pixels[--input];
			pixels[--output] = pixels[--input];
			pixels[--output] = pixels[--input];
		} while (input);
	}
	jpeg_finish_decompress(&cinfo);
	*width = cinfo.output_width;
	*height = cinfo.output_height;
	*pic = pixels;
	jpeg_destroy_decompress(&cinfo);
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
	my_dest_ptr dest = (my_dest_ptr) cinfo->dest;

	jpeg_destroy_compress(cinfo);

	// Make crash fatal or we would probably leak memory.
	Com_Error(ERR_FATAL, "Output buffer for encoded JPEG image has insufficient size of %d bytes", dest->size);

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
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	JSAMPROW row_pointer[1];	/* pointer to JSAMPLE row[s] */
	my_dest_ptr dest;
	int row_stride;		/* physical row width in image buffer */
	size_t outcount;

	/* Step 1: allocate and initialize JPEG compression object */

	cinfo.err = jpeg_std_error(&jerr);
	cinfo.err->error_exit = R_JPGErrorExit;
	cinfo.err->output_message = R_JPGOutputMessage;

	/* Now we can initialize the JPEG compression object. */
	jpeg_create_compress(&cinfo);

	/* Step 2: specify data destination (eg, a file) */
	/* Note: steps 2 and 3 can be done in either order. */

	jpegDest(&cinfo, buffer, bufSize);

	/* Step 3: set parameters for compression */
	cinfo.image_width = image_width; 	/* image width and height, in pixels */
	cinfo.image_height = image_height;
	cinfo.input_components = 3;		/* # of color components per pixel */
	cinfo.in_color_space = JCS_RGB; 	/* colorspace of input image */
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, quality, TRUE /* limit to baseline-JPEG values */);

	/* If quality is set high, disable chroma subsampling */
	if (quality >= 85) {
		cinfo.comp_info[0].h_samp_factor = 1;
		cinfo.comp_info[0].v_samp_factor = 1;
	}

	/* Step 4: Start compressor */

	jpeg_start_compress(&cinfo, TRUE);

	/* Step 5: while (scan lines remain to be written) */
	/*           jpeg_write_scanlines(...); */

	row_stride = image_width * cinfo.input_components + padding; /* JSAMPLEs per row in image_buffer */

	while (cinfo.next_scanline < cinfo.image_height) {
		/* jpeg_write_scanlines expects an array of pointers to scanlines.
		* Here the array is only one element long, but you could pass
		* more than one scanline at a time if that's more convenient.
		*/
		row_pointer[0] = &image_buffer[((cinfo.image_height-1)*row_stride)-cinfo.next_scanline * row_stride];
		(void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	/* Step 6: Finish compression */
	jpeg_finish_compress(&cinfo);

	dest = (my_dest_ptr) cinfo.dest;
	outcount = dest->size - dest->pub.free_in_buffer;

	/* Step 7: release JPEG compression object */
	jpeg_destroy_compress(&cinfo);

	/* And we're done! */
	return outcount;
}


void RE_SaveJPG(const char * filename, int quality, int image_width, int image_height, byte *image_buffer, int padding)
{
	byte *out;
	size_t bufSize;

	bufSize = image_width * image_height * 3;
	out = (byte *)Hunk_AllocateTempMemory(bufSize);

	bufSize = RE_SaveJPGToBuffer(out, bufSize, quality, image_width, image_height, image_buffer, padding);
	ri.FS_WriteFile(filename, out, bufSize);

	Hunk_FreeTempMemory(out);
}

