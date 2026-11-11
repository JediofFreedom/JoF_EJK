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

const int MAX_IMAGE_LOADERS = 10;
struct ImageLoaderMap
{
	const char *extension;
	ImageLoaderFn loader;
} imageLoaders[MAX_IMAGE_LOADERS];
int numImageLoaders;

/*
=================
Finds the image loader associated with the given extension.
=================
*/
const ImageLoaderMap *FindImageLoader ( const char *extension )
{
	for ( int i = 0; i < numImageLoaders; i++ )
	{
		if ( Q_stricmp (extension, imageLoaders[i].extension) == 0 )
		{
			return &imageLoaders[i];
		}
	}

	return NULL;
}

/*
=================
Adds a new image loader to load the specified image file extension.
The 'extension' string should not begin with a period (full stop).
=================
*/
qboolean R_ImageLoader_Add ( const char *extension, ImageLoaderFn imageLoader )
{
	if ( numImageLoaders >= MAX_IMAGE_LOADERS )
	{
		ri.Printf (PRINT_DEVELOPER, "R_AddImageLoader: Cannot add any more image loaders (maximum %d).\n", MAX_IMAGE_LOADERS);
		return qfalse;
	}

	if ( FindImageLoader (extension) != NULL )
	{
		ri.Printf (PRINT_DEVELOPER, "R_AddImageLoader: Image loader already exists for extension \"%s\".\n", extension);
		return qfalse;
	}

	ImageLoaderMap *newImageLoader = &imageLoaders[numImageLoaders];
	newImageLoader->extension = extension;
	newImageLoader->loader = imageLoader;

	numImageLoaders++;

	return qtrue;
}

/*
=================
Initializes the image loader, and adds the built-in
image loaders
=================
*/
void R_ImageLoader_Init()
{
	Com_Memset (imageLoaders, 0, sizeof (imageLoaders));
	numImageLoaders = 0;

	R_ImageLoader_Add ("jpg", LoadJPG);
	R_ImageLoader_Add ("png", LoadPNG);
	R_ImageLoader_Add ("tga", LoadTGA);
}

qboolean R_ImageSourceSignature(const char *shortname, std::vector<byte> &signature, size_t limit,
                               const char *resolvedName) {
	signature.clear();
	if (numImageLoaders != 3 || !ri.FS_GetReadFileIdentity) return qfalse;
	for (int slot = 0; slot < numImageLoaders; ++slot)
		if (imageLoaders[slot].loader != LoadJPG && imageLoaders[slot].loader != LoadPNG && imageLoaders[slot].loader != LoadTGA)
			return qfalse;
	const ImageLoaderMap *preferred = FindImageLoader(COM_GetExtension(shortname));
	char base[MAX_QPATH];
	COM_StripExtension(shortname, base, sizeof(base));
	bool found = false;
	for (int slot = -1; slot < numImageLoaders; ++slot) {
		const auto *loader = slot < 0 ? preferred : &imageLoaders[slot];
		if (!loader || (slot >= 0 && loader == preferred)) continue;
		char candidate[MAX_QPATH];
		if (slot < 0) Q_strncpyz(candidate, shortname, sizeof(candidate));
		else Com_sprintf(candidate, sizeof(candidate), "%s.%s", base, loader->extension);
		fsReadFileIdentity_t identity = {};
		const bool present = ri.FS_GetReadFileIdentity(candidate, &identity, qfalse) != qfalse;
		if (present) {
			// Package members are immutable for the life of this mount. Loose
			// overrides are deliberately decoded normally, including concurrent edits.
			if (identity.provider != 1) { signature.clear(); return qfalse; }
			found = true;
		}
		const size_t nameBytes = strlen(candidate) + 1;
		const size_t overhead = nameBytes + sizeof(identity);
		if (signature.size() > limit || overhead > limit - signature.size()) {
			signature.clear(); return qfalse;
		}
		signature.insert(signature.end(), candidate, candidate + nameBytes);
		const auto *bytes = reinterpret_cast<const byte *>(&identity);
		signature.insert(signature.end(), bytes, bytes + sizeof(identity));
		if (resolvedName && !Q_stricmp(candidate, resolvedName)) {
			// The target must still exist. Earlier candidates, including corrupt
			// preferred files, remain in the signature and invalidate on change.
			if (!present) signature.clear();
			return present ? qtrue : qfalse;
		}
	}
	if (resolvedName) { signature.clear(); return qfalse; }
	return found ? qtrue : qfalse;
}

/*
=================
Loads any of the supported image types into a cannonical
32 bit format.
=================
*/
bool R_ReadEncodedImage(const char *shortname, EncodedImage &image) {
	image = {};
	const ImageLoaderMap *preferred = FindImageLoader(COM_GetExtension(shortname));
	char base[MAX_QPATH];
	COM_StripExtension(shortname, base, sizeof(base));
	for (int slot = -1; slot < numImageLoaders; ++slot) {
		const ImageLoaderMap *loader = slot < 0 ? preferred : &imageLoaders[slot];
		if (!loader || (slot >= 0 && loader == preferred)) continue;
		char candidate[MAX_QPATH];
		if (slot < 0) Q_strncpyz(candidate, shortname, sizeof(candidate));
		else Com_sprintf(candidate, sizeof(candidate), "%s.%s", base, loader->extension);
		void *data = NULL;
		const long length = ri.FS_ReadFile(candidate, &data);
		if (!data) continue;
		const EncodedImageFormat format = loader->loader == LoadJPG ? EncodedImageFormat::JPG : EncodedImageFormat::PNG;
		int width, height;
		if (length <= 0 || (loader->loader != LoadJPG && loader->loader != LoadPNG) ||
			!R_ProbeEncodedImage(static_cast<byte *>(data), length, format, width, height)) {
			ri.FS_FreeFile(data);
			return false; // Preserve loader/extension fallback behavior in the synchronous path.
		}
		image.data = data; image.length = length; image.format = format;
		image.width = width; image.height = height;
		Q_strncpyz(image.resolvedName, candidate, sizeof(image.resolvedName));
		return true;
	}
	return false;
}

void R_LoadImage( const char *shortname, byte **pic, int *width, int *height, char *resolvedName ) {
	if (resolvedName) resolvedName[0] = '\0';
	*pic = NULL;
	*width = 0;
	*height = 0;

	// Try loading the image with the original extension (if possible).
	const char *extension = COM_GetExtension (shortname);
	const ImageLoaderMap *imageLoader = FindImageLoader (extension);
	if ( imageLoader != NULL )
	{
		imageLoader->loader (shortname, pic, width, height);
		if ( *pic )
		{
			if (resolvedName) Q_strncpyz(resolvedName, shortname, MAX_QPATH);
			return;
		}
	}

	// Loop through all the image loaders trying to load this image.
	char extensionlessName[MAX_QPATH];
	COM_StripExtension(shortname, extensionlessName, sizeof( extensionlessName ));
	for ( int i = 0; i < numImageLoaders; i++ )
	{
		const ImageLoaderMap *tryLoader = &imageLoaders[i];
		if ( tryLoader == imageLoader )
		{
			// Already tried this one.
			continue;
		}

		const char *name = va ("%s.%s", extensionlessName, tryLoader->extension);
		tryLoader->loader (name, pic, width, height);
		if ( *pic )
		{
			if (resolvedName) Q_strncpyz(resolvedName, name, MAX_QPATH);
			return;
		}
	}
}
