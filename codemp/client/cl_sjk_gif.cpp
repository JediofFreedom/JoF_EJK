/*
===========================================================================
Copyright (C) 2026, JoF contributors

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

// cl_sjk_gif.cpp -- GIPHY GIFs under SJK chat messages in the chat box (docs/sjk-chat.md,
// "GIFs")
//
// As SJK does it (its chat_gifs), kept simple: the hub carries only the text; this client
// fetches the GIF from GIPHY itself, only by its id, as the 200-pixel-high rendition GIPHY
// makes for every GIF (the original when that one is missing). One worker thread fetches
// and decodes; the main thread only looks up what is ready and draws the frame the
// animation is at. Decoded GIFs stay in memory only, those used least recently making room.
//
// The renderers take raw pixels only through DrawStretchRaw, the cinematics' path: a
// texture whose sides are powers of two, drawn at once in the 640x480 screen. So each frame
// is scaled into such a texture when decoded, laid over a dark background (the texture has
// no alpha), and stretched back to the GIF's shape when drawn.

#include "client.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// The decoder rend2 ships, compiled here for GIFs only, private to this file.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "../../shared/rd-rend2/stb_image.h"

static const DWORD	SJK_GIF_TIMEOUT_MS	= 10000;			// the whole request
static const size_t	SJK_GIF_FILE_MAX	= 8 << 20;			// bytes read at most
static const int	SJK_GIF_CANVAS_MAX	= 2048;				// a side, else refused
static const int	SJK_GIF_FRAMES_MAX	= 240;				// more: the first frame only
static const size_t	SJK_GIF_BYTES_MAX	= 32 << 20;			// a GIF's frames; more: the first only
static const size_t	SJK_GIF_ENTRIES		= 32;				// GIFs kept decoded
static const size_t	SJK_GIF_BUDGET		= 96 << 20;			// bytes for all of them
static const int	SJK_GIF_IN_USE_MS	= 2000;				// drawn this recently: never dropped
static const int	SJK_GIF_RETRY_MS	= 5 * 60 * 1000;	// a GIF that could not be had
static const int	SJK_GIF_ROWS		= 128;				// texture height (the box draws ~90 px at 1080p)
static const int	SJK_GIF_COLS_MAX	= 256;
static const int	SJK_GIF_SLOT		= 15;				// the renderers' scratch image (cinematics use the first)
static const byte	SJK_GIF_BACKDROP	= 24;				// what transparent pixels show

// A decoded GIF: its frames, each cols x rows RGBA, and when each ends in its loop.
struct sjkGif_t {
	int					width = 0, height = 0;	// its shape
	int					cols = 0, rows = 0;		// each frame's texture
	std::vector<byte>	pixels;
	std::vector<int>	ends;					// ms into the loop each frame ends at
	size_t				Stride() const { return (size_t)cols * rows * 4; }
};

struct sjkGifEntry_t {
	int									state = SJK_GIF_LOADING;
	std::shared_ptr<const sjkGif_t>		gif;
	int									lastUsed = 0;
	int									failedAt = 0;
};

// Never freed: the worker may still be waiting on GIPHY when the client quits.
struct sjkGifCache_t {
	std::mutex								lock;
	std::condition_variable					wake;
	std::map<std::string, sjkGifEntry_t>	entries;	// by GIPHY id
	std::deque<std::string>					queue;
	bool									started = false;
};
static sjkGifCache_t *s_gifs = new sjkGifCache_t;

static int SJK_GifPow2( int n ) {
	int p = 1;
	while ( p < n )
		p <<= 1;
	return p;
}

// A GIPHY id as cl_sjk.cpp hands one: ASCII letters and digits only, so it goes into
// GIPHY's path as it is.
static bool SJK_GifIdOk( const char *id ) {
	const size_t length = strlen( id );
	if ( length < 6 || length > 40 )
		return false;
	for ( size_t i = 0; i < length; i++ ) {
		const unsigned char c = id[i];
		if ( c > 127 || !isalnum( c ) )
			return false;
	}
	return true;
}

/*
===============================================================================

FETCH

===============================================================================
*/

// GET https://media.giphy.com<path>: its status (404 for GIPHY's own "not found" picture,
// which it marks with x-retry-not-found-metric), 0 when no whole answer came. HTTPS only,
// no redirect, no cookie, nothing about the player.
static int SJK_GifGet( const std::wstring &path, std::string *body ) {
	int status = 0;
	const ULONGLONG deadline = GetTickCount64() + SJK_GIF_TIMEOUT_MS;
	HINTERNET session = WinHttpOpen( L"EJK", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
	if ( !session )
		session = WinHttpOpen( L"EJK", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
	if ( !session )
		return 0;
	WinHttpSetTimeouts( session, SJK_GIF_TIMEOUT_MS, SJK_GIF_TIMEOUT_MS, SJK_GIF_TIMEOUT_MS, SJK_GIF_TIMEOUT_MS );
	HINTERNET connection = WinHttpConnect( session, L"media.giphy.com", INTERNET_DEFAULT_HTTPS_PORT, 0 );
	HINTERNET request = connection ? WinHttpOpenRequest( connection, L"GET", path.c_str(), NULL, WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE ) : NULL;
	if ( request ) {
		DWORD off = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES;
		WinHttpSetOption( request, WINHTTP_OPTION_DISABLE_FEATURE, &off, sizeof( off ) );
		if ( WinHttpSendRequest( request, L"Accept: image/gif\r\n", (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0 )
				&& WinHttpReceiveResponse( request, NULL ) ) {
			DWORD code = 0, size = sizeof( code );
			WinHttpQueryHeaders( request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
				&code, &size, WINHTTP_NO_HEADER_INDEX );
			DWORD markSize = 0;
			const bool notFound = !WinHttpQueryHeaders( request, WINHTTP_QUERY_CUSTOM, L"x-retry-not-found-metric", NULL, &markSize,
				WINHTTP_NO_HEADER_INDEX ) && GetLastError() == ERROR_INSUFFICIENT_BUFFER;
			if ( notFound ) {
				status = 404;
			} else if ( code != 200 ) {
				status = (int)code;
			} else {
				char buffer[16 * 1024];
				for ( ;; ) {
					DWORD got = 0;
					if ( !WinHttpReadData( request, buffer, sizeof( buffer ), &got ) || body->size() + got > SJK_GIF_FILE_MAX
							|| GetTickCount64() > deadline ) {
						body->clear();
						break;
					}
					if ( !got ) {
						status = 200;
						break;
					}
					body->append( buffer, got );
				}
			}
		}
	}
	if ( request )
		WinHttpCloseHandle( request );
	if ( connection )
		WinHttpCloseHandle( connection );
	WinHttpCloseHandle( session );
	return status;
}

/*
===============================================================================

DECODE

===============================================================================
*/

// One frame (w x h RGBA) into its texture (cols x rows), bilinear, over the backdrop.
static void SJK_GifScale( const stbi_uc *src, int w, int h, byte *dst, int cols, int rows ) {
	for ( int y = 0; y < rows; y++ ) {
		float fy = ( y + 0.5f ) * h / rows - 0.5f;
		fy = fy < 0 ? 0 : fy > h - 1 ? (float)( h - 1 ) : fy;
		const int y0 = (int)fy, y1 = y0 + 1 < h ? y0 + 1 : y0;
		const float ty = fy - y0;
		for ( int x = 0; x < cols; x++ ) {
			float fx = ( x + 0.5f ) * w / cols - 0.5f;
			fx = fx < 0 ? 0 : fx > w - 1 ? (float)( w - 1 ) : fx;
			const int x0 = (int)fx, x1 = x0 + 1 < w ? x0 + 1 : x0;
			const float tx = fx - x0;
			const stbi_uc *p[4] = { src + ( y0 * w + x0 ) * 4, src + ( y0 * w + x1 ) * 4, src + ( y1 * w + x0 ) * 4, src + ( y1 * w + x1 ) * 4 };
			const float k[4] = { ( 1 - tx ) * ( 1 - ty ), tx * ( 1 - ty ), ( 1 - tx ) * ty, tx * ty };
			float rgb[3] = { 0, 0, 0 }, a = 0;
			for ( int i = 0; i < 4; i++ ) {
				const float pa = p[i][3] / 255.0f * k[i];
				rgb[0] += p[i][0] * pa;
				rgb[1] += p[i][1] * pa;
				rgb[2] += p[i][2] * pa;
				a += pa;
			}
			byte *out = dst + ( y * cols + x ) * 4;
			for ( int c = 0; c < 3; c++ )
				out[c] = (byte)( rgb[c] + SJK_GIF_BACKDROP * ( 1 - a ) + 0.5f );
			out[3] = 255;
		}
	}
}

// A GIF file's frames, each as the file composes it; NULL for anything else, a canvas
// over SJK_GIF_CANVAS_MAX a side, or no frame. Past SJK_GIF_FRAMES_MAX frames or
// SJK_GIF_BYTES_MAX, the first frame only, still. Delays under 20 ms count as 100 ms, as
// browsers do.
static std::shared_ptr<sjkGif_t> SJK_GifDecode( const std::string &file ) {
	const stbi_uc *data = (const stbi_uc *)file.data();
	if ( file.size() < 10 || memcmp( data, "GIF8", 4 ) )
		return nullptr;
	const int canvasW = data[6] | ( data[7] << 8 ), canvasH = data[8] | ( data[9] << 8 );
	if ( !canvasW || !canvasH || canvasW > SJK_GIF_CANVAS_MAX || canvasH > SJK_GIF_CANVAS_MAX )
		return nullptr;

	std::shared_ptr<sjkGif_t> gif = std::make_shared<sjkGif_t>();
	stbi__context context;
	stbi__start_mem( &context, data, (int)file.size() );
	stbi__gif g;
	memset( &g, 0, sizeof( g ) );
	std::vector<stbi_uc> recent[2];		// the last two frames, for disposal to "previous"
	int frames = 0, comp = 0, loop = 0;
	bool still = false;
	for ( ;; ) {
		stbi_uc *twoBack = frames >= 2 ? recent[frames % 2].data() : NULL;
		stbi_uc *frame = stbi__gif_load_next( &context, &g, &comp, 4, twoBack );
		if ( !frame || frame == (stbi_uc *)&context )
			break;
		if ( !frames ) {
			if ( g.w <= 0 || g.h <= 0 || g.w > SJK_GIF_CANVAS_MAX || g.h > SJK_GIF_CANVAS_MAX )
				break;
			gif->width = g.w;
			gif->height = g.h;
			gif->rows = SJK_GifPow2( g.h < SJK_GIF_ROWS ? g.h : SJK_GIF_ROWS );
			const int cols = (int)( (float)g.w * gif->rows / g.h + 0.5f );
			gif->cols = SJK_GifPow2( cols < 1 ? 1 : cols > SJK_GIF_COLS_MAX ? SJK_GIF_COLS_MAX : cols );
		}
		if ( frames == SJK_GIF_FRAMES_MAX || ( frames + 1 ) * gif->Stride() > SJK_GIF_BYTES_MAX ) {
			still = true;
			break;
		}
		const size_t size = (size_t)g.w * g.h * 4;
		recent[frames % 2].assign( frame, frame + size );
		gif->pixels.resize( ( frames + 1 ) * gif->Stride() );
		SJK_GifScale( frame, g.w, g.h, gif->pixels.data() + frames * gif->Stride(), gif->cols, gif->rows );
		loop += g.delay < 20 ? 100 : g.delay;
		gif->ends.push_back( loop );
		frames++;
	}
	STBI_FREE( g.out );
	STBI_FREE( g.history );
	STBI_FREE( g.background );
	if ( !frames )
		return nullptr;
	if ( still ) {
		gif->pixels.resize( gif->Stride() );
		gif->ends.assign( 1, 100 );
	}
	return gif;
}

/*
===============================================================================

WORKER

===============================================================================
*/

// Room for a new GIF: while over SJK_GIF_ENTRIES or SJK_GIF_BUDGET, the GIF used least
// recently goes, never one drawn in the last SJK_GIF_IN_USE_MS. Under the lock.
static void SJK_GifMakeRoom( int now ) {
	for ( ;; ) {
		size_t count = 0, bytes = 0;
		std::map<std::string, sjkGifEntry_t>::iterator oldest = s_gifs->entries.end();
		for ( auto it = s_gifs->entries.begin(); it != s_gifs->entries.end(); ++it ) {
			if ( !it->second.gif )
				continue;
			count++;
			bytes += it->second.gif->pixels.size();
			if ( now - it->second.lastUsed > SJK_GIF_IN_USE_MS && ( oldest == s_gifs->entries.end() || it->second.lastUsed < oldest->second.lastUsed ) )
				oldest = it;
		}
		if ( ( count <= SJK_GIF_ENTRIES && bytes <= SJK_GIF_BUDGET ) || oldest == s_gifs->entries.end() )
			return;
		s_gifs->entries.erase( oldest );
	}
}

static void SJK_GifWorker( void ) {
	for ( ;; ) {
		std::string id;
		{
			std::unique_lock<std::mutex> hold( s_gifs->lock );
			s_gifs->wake.wait( hold, []{ return !s_gifs->queue.empty(); } );
			id = s_gifs->queue.front();
			s_gifs->queue.pop_front();
		}
		// GIPHY's 200-pixel-high rendition, never the pasted address; the original only
		// when GIPHY has no such rendition.
		std::string file;
		const std::wstring wideId( id.begin(), id.end() );
		int status = SJK_GifGet( L"/media/" + wideId + L"/200.gif", &file );
		if ( status == 404 ) {
			file.clear();
			status = SJK_GifGet( L"/media/" + wideId + L"/giphy.gif", &file );
		}
		std::shared_ptr<sjkGif_t> gif = status == 200 ? SJK_GifDecode( file ) : nullptr;

		std::lock_guard<std::mutex> hold( s_gifs->lock );
		auto it = s_gifs->entries.find( id );
		if ( it == s_gifs->entries.end() )
			continue;
		const int now = Sys_Milliseconds();
		if ( gif ) {
			it->second.state = SJK_GIF_DRAWN;
			it->second.gif = gif;
			it->second.lastUsed = now;
			SJK_GifMakeRoom( now );
		} else {
			it->second.state = SJK_GIF_UNAVAILABLE;
			it->second.failedAt = now;
		}
	}
}

/*
===============================================================================

DRAW

===============================================================================
*/

// GIF id fitted into x, y, w, h of the 640x480 screen, at its left, in its shape, at the
// frame its animation is at (every copy plays in step). SJK_GIF_LOADING the first time
// and until it came, SJK_GIF_UNAVAILABLE when it could not be had (asked for again after
// SJK_GIF_RETRY_MS), SJK_GIF_OFF with cl_sjkChatGifs 0; the cgame draws those itself.
int CL_SJK_DrawGif( const char *id, float x, float y, float w, float h ) {
	if ( !cl_sjkChatGifs || !cl_sjkChatGifs->integer )
		return SJK_GIF_OFF;
	if ( !id || !SJK_GifIdOk( id ) || w <= 0 || h <= 0 )
		return SJK_GIF_UNAVAILABLE;

	const int now = Sys_Milliseconds();
	std::shared_ptr<const sjkGif_t> gif;
	{
		std::lock_guard<std::mutex> hold( s_gifs->lock );
		auto it = s_gifs->entries.find( id );
		bool ask = false;
		if ( it == s_gifs->entries.end() ) {
			// Ids that could not be had would pile up otherwise.
			if ( s_gifs->entries.size() >= 256 ) {
				for ( auto old = s_gifs->entries.begin(); old != s_gifs->entries.end(); ) {
					if ( old->second.state == SJK_GIF_UNAVAILABLE )
						old = s_gifs->entries.erase( old );
					else
						++old;
				}
			}
			it = s_gifs->entries.emplace( id, sjkGifEntry_t() ).first;
			ask = true;
		} else if ( it->second.state == SJK_GIF_UNAVAILABLE && now - it->second.failedAt > SJK_GIF_RETRY_MS ) {
			it->second.state = SJK_GIF_LOADING;
			ask = true;
		}
		if ( ask ) {
			s_gifs->queue.push_back( id );
			if ( !s_gifs->started ) {
				std::thread( SJK_GifWorker ).detach();
				s_gifs->started = true;
			}
			s_gifs->wake.notify_one();
		}
		it->second.lastUsed = now;
		if ( it->second.state != SJK_GIF_DRAWN )
			return it->second.state;
		gif = it->second.gif;
	}

	size_t frame = 0;
	const int loop = gif->ends.back();
	if ( gif->ends.size() > 1 && loop > 0 ) {
		const int at = now % loop;
		while ( frame + 1 < gif->ends.size() && at >= gif->ends[frame] )
			frame++;
	}

	// The 640x480 screen is stretched over the window: a unit is wider than high on a
	// wide screen, so the GIF's width in units is its height times its shape, narrowed.
	const float unitW = cls.glconfig.vidWidth / 640.0f, unitH = cls.glconfig.vidHeight / 480.0f;
	float drawH = h, drawW = h * gif->width / gif->height * ( unitW > 0 ? unitH / unitW : 1.0f );
	if ( drawW > w ) {
		drawH *= w / drawW;
		drawW = w;
	}
	re->DrawStretchRaw( (int)x, (int)y, (int)( drawW + 0.5f ), (int)( drawH + 0.5f ), gif->cols, gif->rows,
		gif->pixels.data() + frame * gif->Stride(), SJK_GIF_SLOT, qtrue );
	return SJK_GIF_DRAWN;
}

#else // !_WIN32

int CL_SJK_DrawGif( const char *id, float x, float y, float w, float h ) {
	return SJK_GIF_OFF;
}

#endif // _WIN32
