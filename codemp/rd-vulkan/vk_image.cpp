/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
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

#include "tr_local.h"
#include "vk_resident_cache.h"
#include "vk_host_image_copy.h"
#include "vk_sync2.h"
#include "vk_texture_budget.h"
#include "vk_device_extensions.h"
#include "vk_texture_jobs.h"
#include "vk_texture_context.h"
#include "vk_texture_scratch.h"
#include "vk_prepared_cache.h"
#include "../rd-common/tr_image_decode.h"
#include <chrono>
#include <atomic>
#include "vk_load_profile.h"
#include "vk_allocation_ranges.h"
vkLoadStats_t vkLoadStats = {};

#define STB_DXT_IMPLEMENTATION
#include "utils/stb_dxt.h"

#define	DEFAULT_SIZE	16
#define FILE_HASH_SIZE	1024
#define	DLIGHT_SIZE		16
#define	FOG_S			256
#define	FOG_T			32

static image_t *hashTable[FILE_HASH_SIZE];

static size_t residentTextureCount, residentTextureLimit, residentPageCount, residentPageBytes;
struct ResidentImagePage {
	VkDevice device;
	VkDeviceMemory memory;
	size_t bytes;
	uint32_t memoryType;
	AllocationRanges ranges;
	ResidentImagePage(VkDeviceMemory allocation, size_t size, uint32_t type) : device(vk.device), memory(allocation),
		bytes(size), memoryType(type), ranges(size) { ++residentPageCount; residentPageBytes += bytes; }
	~ResidentImagePage() { VK_FREE_MEMORY(device, memory); --residentPageCount; residentPageBytes -= bytes; }
};
static std::vector<std::weak_ptr<ResidentImagePage>> residentImagePages;
struct ResidentTexture {
	VkDevice device;
	VkImage image;
	VkImageView view;
	std::shared_ptr<ResidentImagePage> page;
	size_t offset, bytes;
	uint32_t width, height, uploadWidth, uploadHeight, mipLevels;
	int format;
	std::string key, sourceName;
	ResidentTexture(image_t *source, size_t allocationBytes, std::shared_ptr<ResidentImagePage> owner, size_t position) :
		device(vk.device), image(VK_NULL_HANDLE), view(VK_NULL_HANDLE), page(std::move(owner)), offset(position), bytes(allocationBytes),
		width(source->width), height(source->height), uploadWidth(source->uploadWidth), uploadHeight(source->uploadHeight),
		mipLevels(source->mipLevels), format(source->internalFormat) { ++residentTextureCount; }
	~ResidentTexture() {
		if (view) qvkDestroyImageView(device, view, NULL);
		if (image) qvkDestroyImage(device, image, NULL);
		page->ranges.release(offset, bytes);
		--residentTextureCount;
	}
};
static ResidentResourceCache<ResidentTexture> residentTextures;
static std::unordered_map<image_t *, std::shared_ptr<ResidentTexture>> activeResidentTextures;
static cvar_t *textureSignaturePrefix;

static void R_InitResidentTextures() {
	textureSignaturePrefix = ri.Cvar_Get("r_textureSignaturePrefix", "1", CVAR_ARCHIVE_ND,
		"Validate texture sources only through the previously decoded winner (experimental)");
	ri.Cvar_CheckRange(textureSignaturePrefix, 0, 1, qtrue);
	cvar_t *budget = ri.Cvar_Get("r_textureCacheMegs", "-1", CVAR_ARCHIVE_ND,
		"GPU textures retained between maps: -1 chooses a device-scaled budget, 0 disables, positive values set MiB");
	ri.Cvar_CheckRange(budget, -1, 2048, qtrue);
	VkPhysicalDeviceProperties properties;
	qvkGetPhysicalDeviceProperties(vk.physical_device, &properties);
	qvkGetPhysicalDeviceMemoryProperties(vk.physical_device, &vk.devMemProperties);
	residentTextureLimit = std::min<size_t>(128, properties.limits.maxMemoryAllocationCount / 4);
	VkDeviceSize deviceMemory = 0;
	uint32_t selectedHeap = 0;
	for (uint32_t i = 0; i < vk.devMemProperties.memoryHeapCount; ++i)
		if ((vk.devMemProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) &&
			vk.devMemProperties.memoryHeaps[i].size > deviceMemory) {
			deviceMemory = vk.devMemProperties.memoryHeaps[i].size;
			selectedHeap = i;
		}
	VkPhysicalDeviceMemoryBudgetPropertiesEXT feedback = {};
	feedback.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
	bool hasBudget = false;
	if (vk.instanceApiVersion >= VK_API_VERSION_1_1 &&
		VK_HasDeviceExtension(vk.physical_device, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME, qvkEnumerateDeviceExtensionProperties)) {
		const auto query = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties2>(
			qvkGetInstanceProcAddr(vk.instance, "vkGetPhysicalDeviceMemoryProperties2"));
		if (query) {
			VkPhysicalDeviceMemoryProperties2 memory = {};
			memory.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
			memory.pNext = &feedback;
			query(vk.physical_device, &memory);
			hasBudget = feedback.heapBudget[selectedHeap] != 0;
		}
	}
	const size_t limit = VK_TextureCacheBudget(budget->integer, sizeof(void *) == 8, deviceMemory,
		hasBudget, feedback.heapBudget[selectedHeap], feedback.heapUsage[selectedHeap], residentPageBytes);
	residentTextures.setLimit(residentTextureLimit ? limit : 0);
	// Logical eviction can leave fragmented pages alive. Reclaim idle resources
	// until physical residency also fits a reduced budget; current owners stay pinned.
	while (residentPageBytes > residentTextures.limit() && residentTextures.evictIdle()) {}
	residentTextures.hits = residentTextures.misses = residentTextures.evictions = 0;
	if (ri.Cvar_VariableIntegerValue("cl_loadTimings"))
		ri.Printf(PRINT_ALL, "LOADPROFILE Vulkan texture budget: %u MiB (%s), driver budget feedback %s\n",
			(unsigned)(residentTextures.limit() / (1024 * 1024)), budget->integer < 0 ? "automatic" : "explicit",
			hasBudget ? "available" : "unavailable");
}

void vk_clear_texture_cache() {
	// Called after map image owners are released, before the Vulkan device dies.
	residentTextures.clear();
	residentImagePages.clear();
	assert(activeResidentTextures.empty() && residentTextureCount == 0 && residentPageBytes == 0);
	if (ri.Cvar_VariableIntegerValue("cl_loadTimings"))
		ri.Printf(PRINT_ALL, "LOADPROFILE resident cache shutdown: %u textures, %u pages\n", (unsigned)residentTextureCount, (unsigned)residentPageCount);
}

void vk_report_texture_cache() {
	if (ri.Cvar_VariableIntegerValue("cl_loadTimings"))
		ri.Printf(PRINT_ALL, "LOADPROFILE resident textures: %u hits, %u misses, %u retained, %.1f MiB retained, %u textures, %u GPU allocations\n",
			(unsigned)residentTextures.hits, (unsigned)residentTextures.misses,
			(unsigned)residentTextures.size(), residentTextures.bytes() / (1024.0 * 1024.0), (unsigned)residentTextureCount, (unsigned)residentPageCount);
}

static TexturePreparationContext R_CaptureTextureContext() {
    TexturePreparationContext context = {};
    context.roundDown = r_roundImagesDown->integer; context.picmip = r_picmip->integer;
    context.nomip = r_nomip->integer; context.textureBits = r_texturebits->integer;
    context.simpleMipMaps = r_simpleMipMaps->integer; context.colorMipLevels = r_colorMipLevels->integer;
    context.smartPicmip = r_smartpicmip->integer; context.maxTextureSize = glConfig.maxTextureSize;
    context.compressedFormat = vk.compressed_format; context.supportsGamma = glConfig.deviceSupportsGamma;
    context.fboActive = vk.fboActive; context.mapLoading = tr.mapLoading;
    context.mapGreyInteger = r_mapGreyScale->integer; context.mapGreyScale = r_mapGreyScale->value;
    Com_Memcpy(context.gamma, s_gammatable, sizeof(context.gamma));
    Com_Memcpy(context.intensity, s_intensitytable, sizeof(context.intensity));
    return context;
}

static std::string R_ResidentTextureKey(const char *name, imgFlags_t flags, const TexturePreparationContext &context) {
	std::string key(name);
	for (char &ch : key) {
		if (ch == '\\') ch = '/';
		else if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
	}
	key.push_back('\0');
    const int settings[] = {int(flags), context.roundDown, context.picmip, context.nomip,
        context.textureBits, context.compressedFormat, context.simpleMipMaps, context.colorMipLevels,
        context.smartPicmip, context.maxTextureSize, context.supportsGamma, context.fboActive, context.mapLoading,
        context.mapGreyInteger};
    key.append(reinterpret_cast<const char *>(settings), sizeof(settings));
    key.append(reinterpret_cast<const char *>(&context.mapGreyScale), sizeof(context.mapGreyScale));
    key.append(reinterpret_cast<const char *>(context.gamma), sizeof(context.gamma));
    key.append(reinterpret_cast<const char *>(context.intensity), sizeof(context.intensity));
	return key;
}

static void R_InvalidateResidentTexture(image_t *image) {
	const auto found = activeResidentTextures.find(image);
	if (found != activeResidentTextures.end())
		residentTextures.erase(found->second->key, found->second);
}

int		gl_filter_min = GL_LINEAR_MIPMAP_NEAREST;
int		gl_filter_max = GL_LINEAR;

textureMode_t modes[] = {
	{"GL_NEAREST", GL_NEAREST, GL_NEAREST},
	{"GL_LINEAR", GL_LINEAR, GL_LINEAR},
	{"GL_NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST},
	{"GL_LINEAR_MIPMAP_NEAREST", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR},
	{"GL_NEAREST_MIPMAP_LINEAR", GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST},
	{"GL_LINEAR_MIPMAP_LINEAR", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR}
};

void vk_update_descriptor_set( image_t *image, qboolean mipmap );

/*
=================
Pool and scratch
=================
*/
#define IMAGE_POOL_INITIAL_CAPACITY 1024
#define IMAGE_UPLOAD_SCRATCH_MIN    (64 * 1024 * 1024)
#define IMAGE_RESAMPLE_SCRATCH_MIN  (32 * 1024 * 1024)

typedef struct {
	byte	*buffer;
	size_t	size;
} imageScratchBuffer_t;

typedef struct imageScratch_s {
	imageScratchBuffer_t upload;
	imageScratchBuffer_t resample;
	imageScratchBuffer_t mip;
	imageScratchBuffer_t compress;
	imageScratchBuffer_t convert;
} imageScratch_t;

static imageScratch_t s_imageScratch;

static byte* R_ImageScratchAlloc( imageScratchBuffer_t *scratch, size_t size )
{
	if ( scratch->size < size )
	{
		ri.Printf(
			PRINT_ALL,
			S_COLOR_YELLOW "Resizing image scratch buffer from %.2f MB to %.2f MB\n",
			(float)scratch->size / (1024.0f * 1024.0f),
			(float)size / (1024.0f * 1024.0f)
		);

		if ( scratch->buffer )
			ri.Z_Free(scratch->buffer);

		scratch->buffer = (byte*)Z_Malloc( size, TAG_TEMP_IMAGE );
		scratch->size = size;
	}

	return scratch->buffer;
}

void R_InitImageScratch(void)
{
	Com_Memset( &s_imageScratch, 0, sizeof(s_imageScratch) );
}

static void R_FreeScratch( imageScratchBuffer_t* scratch )
{
	if ( scratch->buffer )
		Z_Free(scratch->buffer);

	scratch->buffer = NULL;
	scratch->size = 0;
}

void R_DestroyImageScratch( void )
{
	R_FreeScratch( &s_imageScratch.upload );
	R_FreeScratch( &s_imageScratch.resample );
	R_FreeScratch( &s_imageScratch.mip );
	R_FreeScratch( &s_imageScratch.compress );
	R_FreeScratch( &s_imageScratch.convert );

	Com_Memset( &s_imageScratch, 0, sizeof(s_imageScratch) );
}

void R_InitImagesPool( void )
{
    tr.images.capacity = IMAGE_POOL_INITIAL_CAPACITY;
    tr.images.count = 0;

    tr.images.items = (image_t**)Z_Malloc( sizeof(image_t*) * tr.images.capacity, TAG_IMAGE_T);
}

static void R_DestroyImagesPool( void )
{
	uint32_t i;

	for ( i = 0; i < tr.images.count; i++ ) {
		ri.Z_Free( tr.images.items[i] );
	}

	ri.Z_Free( tr.images.items );

	tr.images.items = NULL;
	tr.images.count = 0;
	tr.images.capacity = 0;
}

static void R_AddImageToPool(image_t *image)
{
    if (tr.images.count >= tr.images.capacity)
    {
        uint32_t new_capacity = tr.images.capacity * 2;

		ri.Printf( PRINT_ALL, S_COLOR_YELLOW "Resizing image pool from %u to %u entries\n", tr.images.capacity, new_capacity );

        image_t **new_items = (image_t**)Z_Malloc(sizeof(image_t*) * new_capacity, TAG_IMAGE_T);
		Com_Memset(new_items, 0, sizeof(*new_items) * new_capacity);

        Com_Memcpy( new_items, tr.images.items, sizeof(image_t*) * tr.images.count );

        ri.Z_Free(tr.images.items);

        tr.images.items = new_items;
        tr.images.capacity = new_capacity;
    }

    image->index = tr.images.count;
    tr.images.items[tr.images.count++] = image;
}

/*
=================
GetTextureMode
=================
*/
textureMode_t *GetTextureMode( const char *name )
{
	size_t i;

	for ( i = 0; i < ARRAY_LEN( modes ); i++ ) {
		if ( !Q_stricmp( modes[i].name, name ) ) {
			return &modes[i];
		}
	}

	return NULL;
}

void vk_texture_mode( const char *string, const qboolean init ) {
	const textureMode_t *mode;
	image_t		*img;
	uint32_t	i;

	mode = GetTextureMode( string );

	if ( mode == NULL ) {
		ri.Printf( PRINT_ALL, "bad texture filter name '%s'\n", string );
		return;
	}

	gl_filter_min = mode->minimize;
	gl_filter_max = mode->maximize;

	if( init ){
		r_textureMode->modified = qfalse; // no need to do this a seccond time in tr_cmds
		return;
	}

	if ( gl_filter_min == vk.samplers.filter_min && gl_filter_max == vk.samplers.filter_max ) {
		return;
	}

	vk_wait_idle();
	vk_destroy_samplers();

	vk.samplers.filter_min = gl_filter_min;
	vk.samplers.filter_max = gl_filter_max;
	vk_update_attachment_descriptors();
	for ( i = 0; i < tr.images.count; i++ ) {
		img = tr.images.items[i];
		// Destroying the sampler cache invalidates non-mip descriptors too,
		// even though their fixed linear filtering does not change.
		vk_update_descriptor_set( img, (img->flags & IMGFLAG_MIPMAP) ? qtrue : qfalse );
	}
}

static int generateHashValue( const char *fname )
{
    uint32_t i = 0;
    int	hash = 0;

    while (fname[i] != '\0') {
        char letter = tolower(fname[i]);
        if (letter == '.') break;		// don't include extension
        if (letter == '\\') letter = '/';	// damn path names
        hash += (long)(letter) * (i + 119);
        i++;
    }
    hash &= (FILE_HASH_SIZE - 1);
    return hash;
}

size_t cstrlen( const char *str )
{
    const char *s;

    for (s = str; *s; ++s)
        ;
    return (s - str);
}

void vk_destroy_samplers( void )
{
	int i;

	for ( i = 0; i < vk.samplers.count; i++ ) {
		qvkDestroySampler( vk.device, vk.samplers.handle[i], NULL );
		memset( &vk.samplers.def[i], 0x0, sizeof( vk.samplers.def[i] ) );
		vk.samplers.handle[i] = VK_NULL_HANDLE;
	}

	vk.samplers.count = 0;
}

VkSampler vk_find_sampler( const Vk_Sampler_Def *def ) {
	VkSamplerAddressMode address_mode;
	VkSamplerCreateInfo desc;
	VkSampler sampler;
	VkFilter mag_filter;
	VkFilter min_filter;
	VkSamplerMipmapMode mipmap_mode;
	float maxLod;
	int i;

	// Look for sampler among existing samplers.
	for ( i = 0; i < vk.samplers.count; i++ ) {
		const Vk_Sampler_Def *cur_def = &vk.samplers.def[i];
		if( memcmp( cur_def, def, sizeof( *def ) ) == 0 )
		{
			return vk.samplers.handle[i];
		}
	}

	// Create new sampler.
	if (vk.samplers.count >= MAX_VK_SAMPLERS) {
		ri.Error(ERR_DROP, "vk_find_sampler: MAX_VK_SAMPLERS hit\n");
	}

	address_mode = def->address_mode;

	if (def->gl_mag_filter == GL_NEAREST) {
		mag_filter = VK_FILTER_NEAREST;
	}
	else if (def->gl_mag_filter == GL_LINEAR) {
		mag_filter = VK_FILTER_LINEAR;
	}
	else {
		ri.Error(ERR_FATAL, "vk_find_sampler: invalid gl_mag_filter");
		return VK_NULL_HANDLE;
	}

	maxLod = vk.maxLod;

	if (def->gl_min_filter == GL_NEAREST) {
		min_filter = VK_FILTER_NEAREST;
		mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		maxLod = 0.25f; // used to emulate OpenGL's GL_LINEAR/GL_NEAREST minification filter
	}
	else if (def->gl_min_filter == GL_LINEAR) {
		min_filter = VK_FILTER_LINEAR;
		mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		maxLod = 0.25f; // used to emulate OpenGL's GL_LINEAR/GL_NEAREST minification filter
	}
	else if (def->gl_min_filter == GL_NEAREST_MIPMAP_NEAREST) {
		min_filter = VK_FILTER_NEAREST;
		mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	}
	else if (def->gl_min_filter == GL_LINEAR_MIPMAP_NEAREST) {
		min_filter = VK_FILTER_LINEAR;
		mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	}
	else if (def->gl_min_filter == GL_NEAREST_MIPMAP_LINEAR) {
		min_filter = VK_FILTER_NEAREST;
		mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	}
	else if (def->gl_min_filter == GL_LINEAR_MIPMAP_LINEAR) {
		min_filter = VK_FILTER_LINEAR;
		mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	}
	else {
		ri.Error(ERR_FATAL, "vk_find_sampler: invalid gl_min_filter");
		return VK_NULL_HANDLE;
	}

	if (def->max_lod_1_0) {
		maxLod = 1.0f;
	}

	desc.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.magFilter = mag_filter;
	desc.minFilter = min_filter;
	desc.mipmapMode = mipmap_mode;
	desc.addressModeU = address_mode;
	desc.addressModeV = address_mode;
	desc.addressModeW = address_mode;
	desc.mipLodBias = 0.0f;

	if ( def->noAnisotropy || mipmap_mode == VK_SAMPLER_MIPMAP_MODE_NEAREST || mag_filter == VK_FILTER_NEAREST ) {
		desc.anisotropyEnable = VK_FALSE;
		desc.maxAnisotropy = 1.0f;
	}
	else {
		desc.anisotropyEnable = ( r_ext_texture_filter_anisotropic->integer && vk.samplerAnisotropy ) ? VK_TRUE : VK_FALSE;
		if ( desc.anisotropyEnable ) {
			desc.maxAnisotropy = MIN( r_ext_max_anisotropy->integer, vk.maxAnisotropy );
		}
	}

	desc.compareEnable = VK_FALSE;
	desc.compareOp = VK_COMPARE_OP_ALWAYS;
	desc.minLod = 0.0f;
	desc.maxLod = (maxLod == vk.maxLod) ? VK_LOD_CLAMP_NONE : maxLod;
	desc.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	desc.unnormalizedCoordinates = VK_FALSE;
	VK_CHECK(qvkCreateSampler(vk.device, &desc, NULL, &sampler));
	VK_SET_OBJECT_NAME(sampler, va("image sampler %i", vk.samplers.count), VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT);

	vk.samplers.def[vk.samplers.count] = *def;
	vk.samplers.handle[vk.samplers.count] = sampler;
	vk.samplers.count++;

	return sampler;
}

uint32_t vk_find_memory_type_lazy( uint32_t memory_type_bits, VkMemoryPropertyFlags properties,
	VkMemoryPropertyFlags *outprops ) {
    VkPhysicalDeviceMemoryProperties memory_properties;
    uint32_t i;

    qvkGetPhysicalDeviceMemoryProperties(vk.physical_device, &memory_properties);

    for (i = 0; i < memory_properties.memoryTypeCount; i++) {
        if ((memory_type_bits & (1 << i)) != 0 && (memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            if (outprops) {
                *outprops = memory_properties.memoryTypes[i].propertyFlags;
            }
            return i;
        }
    }

    return ~0U;

}

uint32_t vk_find_memory_type( uint32_t memory_type_bits, VkMemoryPropertyFlags properties )
{
    VkPhysicalDeviceMemoryProperties memory_properties;
    uint32_t i;

    qvkGetPhysicalDeviceMemoryProperties(vk.physical_device, &memory_properties);

    for (i = 0; i < memory_properties.memoryTypeCount; i++) {
        if ((memory_type_bits & (1 << i)) != 0 &&
            (memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    ri.Error(ERR_FATAL, "Vulkan: failed to find matching memory type with requested properties");
    return ~0U;
}

static qboolean RawImage_HasAlpha( const byte *scan, const int numPixels )
{
	int i;

	if (!scan)
		return qtrue;

	for (i = 0; i < numPixels; i++)
	{
		if (scan[i * 4 + 3] != 255)
		{
			return qtrue;
		}
	}

	return qfalse;
}
#if 0
void vk_record_buffer_memory_barrier( VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize size, VkDeviceSize offset,
	VkPipelineStageFlags src_stages, VkPipelineStageFlags dst_stages,
	VkAccessFlags src_access, VkAccessFlags dst_access ) {

	VkBufferMemoryBarrier barrier;
	barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	barrier.pNext = NULL;
	barrier.srcAccessMask = src_access;
	barrier.dstAccessMask = dst_access;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer = buffer;
	barrier.offset = offset;
	barrier.size = size;

	qvkCmdPipelineBarrier(cb, src_stages, dst_stages, 0, 0, NULL, 1, &barrier, 0, NULL);
}
#endif
void vk_record_image_layout_transition( VkCommandBuffer cmdBuf, VkImage image,
	VkImageAspectFlags image_aspect_flags,
	VkImageLayout old_layout, VkImageLayout new_layout, uint32_t src_stage_override, uint32_t dst_stage_override )
{
	VkImageMemoryBarrier barrier;
	uint32_t src_stage, dst_stage;

	switch ( old_layout ) {
		case VK_IMAGE_LAYOUT_UNDEFINED:
			if ( src_stage_override != 0 )
				src_stage = src_stage_override;
			else
				src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			barrier.srcAccessMask = VK_ACCESS_NONE;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			barrier.srcAccessMask = VK_ACCESS_NONE;
			break;
		default:
			ri.Error( ERR_DROP, "unsupported old layout %i", old_layout );
			src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			barrier.srcAccessMask = VK_ACCESS_NONE;
			break;
	}

	switch ( new_layout ) {
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			dst_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			barrier.dstAccessMask = VK_ACCESS_NONE;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
			break;
		default:
			ri.Error( ERR_DROP, "unsupported new layout %i", new_layout);
			dst_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			barrier.dstAccessMask = VK_ACCESS_NONE;
			break;
	}

	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.pNext = NULL;
	//barrier.srcAccessMask = src_access_flags;
	//barrier.dstAccessMask = dst_access_flags;
	barrier.oldLayout = old_layout;
	barrier.newLayout = new_layout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = image_aspect_flags;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;


	VK_ImageBarrier(cmdBuf, src_stage, dst_stage, barrier, qvkCmdPipelineBarrier, vk.synchronization2 ? qvkCmdPipelineBarrier2 : nullptr);
}

void vk_upload_image( image_t *image, byte *pic ) {

	Image_Upload_Data upload_data;
	int w, h;

	vk_generate_image_upload_data( image, pic, &upload_data );

	w = upload_data.base_level_width;
	h = upload_data.base_level_height;

	image->uploadWidth = w;
	image->uploadHeight = h;

	vk_create_image( image, w, h, upload_data.mip_levels );
	vk_upload_image_data( image, 0, 0, w, h, upload_data.mip_levels, upload_data.buffer, upload_data.buffer_size, qfalse );

	//ri.Hunk_FreeTempMemory( upload_data.buffer );
	//upload_data.buffer = NULL;
}

static int R_CalcUploadSize(
	int width,
	int height,
	qboolean mipmap,
	qboolean compressed,
	int bytesPerPixel)
{
	int total = 0;

	while (1)
	{
		if (compressed)
			total += rygCompressedSize(width, height);
		else
			total += width * height * bytesPerPixel;

		if (!mipmap || (width == 1 && height == 1))
			break;

		width >>= 1;
		height >>= 1;

		if (width < 1) width = 1;
		if (height < 1) height = 1;
	}

	return total;
}

bool vk_generate_image_upload_data(image_t* image, byte* data, Image_Upload_Data* upload_data,
    const TexturePreparationContext *context, TexturePreparationBuffers *scratch)
{
    uint64_t privateElapsed = 0;
    ImagePhaseTimer profile(scratch ? privateElapsed : vkLoadStats.imagePrepareUs);
    if (scratch && (!context || !data || (image->flags & (IMGFLAG_LIGHTMAP | IMGFLAG_COLORSHIFT)))) return false;
	qboolean mipmap = (image->flags & IMGFLAG_MIPMAP) ? qtrue : qfalse;
	qboolean picmip = (image->flags & IMGFLAG_PICMIP) ? qtrue : qfalse;

	byte* resampled_buffer = NULL;
	byte* compressed_buffer = NULL;
	byte* mip_buffer = NULL;

	int scaled_width, scaled_height;
	int width = image->width;
	int height = image->height;

	int mip_level_size;
	int miplevel;

	qboolean compressed = qfalse;
	const int bytesPerPixel = 4;
	int write_offset = 0;

	if (image->flags & IMGFLAG_NOSCALE) {
		scaled_width = width;
		scaled_height = height;
	}
	else {
		for (scaled_width = 1; scaled_width < width; scaled_width <<= 1);
		for (scaled_height = 1; scaled_height < height; scaled_height <<= 1);

		if ((context ? context->roundDown : r_roundImagesDown->integer) && scaled_width > width)
			scaled_width >>= 1;

		if ((context ? context->roundDown : r_roundImagesDown->integer) && scaled_height > height)
			scaled_height >>= 1;
	}

	if (picmip && ((context ? context->mapLoading : tr.mapLoading) || (context ? context->nomip : r_nomip->integer) == 0)) {
		scaled_width >>= (context ? context->picmip : r_picmip->integer);
		scaled_height >>= (context ? context->picmip : r_picmip->integer);
	}

	if (scaled_width < 1)
		scaled_width = 1;

	if (scaled_height < 1)
		scaled_height = 1;

	if (!(image->flags & IMGFLAG_LIGHTMAP)) {
		while (scaled_width > (context ? context->maxTextureSize : glConfig.maxTextureSize) || scaled_height > (context ? context->maxTextureSize : glConfig.maxTextureSize)) {
			scaled_width = MAX(1, scaled_width >> 1);
			scaled_height = MAX(1, scaled_height >> 1);
		}
	}

	upload_data->base_level_width = scaled_width;
	upload_data->base_level_height = scaled_height;

	if ((context ? context->textureBits : r_texturebits->integer) > 16 || (context ? context->textureBits : r_texturebits->integer) == 0 || (image->flags & IMGFLAG_LIGHTMAP)) {
		if (!(context ? context->compressedFormat : vk.compressed_format) || (image->flags & IMGFLAG_NO_COMPRESSION)) {
			image->internalFormat = VK_FORMAT_R8G8B8A8_UNORM;
		}
		else {
			image->internalFormat = (context ? context->compressedFormat : vk.compressed_format);
			compressed = qtrue;
		}
	}
	else {
		qboolean has_alpha = RawImage_HasAlpha(data, width * height);

		image->internalFormat = has_alpha ?
			VK_FORMAT_B4G4R4A4_UNORM_PACK16 :
			VK_FORMAT_A1R5G5B5_UNORM_PACK16;

		// Keep CPU mip data in RGBA. vk_upload_image_data converts the complete
		// mip chain to the selected 16-bit GPU format exactly once.
	}

	upload_data->buffer_size = R_CalcUploadSize(
		scaled_width,
		scaled_height,
		mipmap,
		compressed,
		bytesPerPixel);

	upload_data->buffer = scratch ? scratch->allocate(TexturePreparationBuffers::Upload, upload_data->buffer_size) : R_ImageScratchAlloc(
		&s_imageScratch.upload,
		MAX(upload_data->buffer_size, IMAGE_UPLOAD_SCRATCH_MIN)
	);

	if (!upload_data->buffer) return false;
	if (data == NULL) {
		Com_Memset(upload_data->buffer, 0, upload_data->buffer_size);
		upload_data->mip_levels = 1;
		return true;
	}

	if ((scaled_width != width || scaled_height != height) && data) {
		resampled_buffer = scratch ? scratch->allocate(TexturePreparationBuffers::Pixels, size_t(scaled_width) * scaled_height * 4) : R_ImageScratchAlloc(
			&s_imageScratch.resample,
			MAX(scaled_width * scaled_height * 4, IMAGE_RESAMPLE_SCRATCH_MIN)
		);

        if (!resampled_buffer || !ResampleTexture((unsigned*)data, width, height,
            (unsigned*)resampled_buffer, scaled_width, scaled_height, scratch)) return false;

		mip_buffer = resampled_buffer;
	}
	else {
		mip_buffer = scratch ? scratch->allocate(TexturePreparationBuffers::Pixels, size_t(scaled_width) * scaled_height * 4) : R_ImageScratchAlloc(
			&s_imageScratch.mip,
			scaled_width * scaled_height * 4
		);

		if (!mip_buffer) return false;
		Com_Memcpy(mip_buffer, data, scaled_width * scaled_height * 4);
	}

	width = scaled_width;
	height = scaled_height;

	if (image->flags & IMGFLAG_COLORSHIFT) {
		byte* p = mip_buffer;
		int i, n = width * height;

		for (i = 0; i < n; i++, p += 4)
			R_ColorShiftLightingBytes(p, p, qfalse);
	}

	while (width > scaled_width || height > scaled_height) {
		if (!R_MipMap(mip_buffer, mip_buffer, width, height, context, scratch)) return false;

		width >>= 1;
		if (width < 1)
			width = 1;

		height >>= 1;
		if (height < 1)
			height = 1;
	}

	if (!(image->flags & IMGFLAG_NOLIGHTSCALE)) {
		R_LightScaleTexture(mip_buffer, scaled_width, scaled_height, mipmap ? qfalse : qtrue, context);
	}

	if (compressed) {
		mip_level_size = rygCompressedSize(scaled_width, scaled_height);

		compressed_buffer = scratch ? scratch->allocate(TexturePreparationBuffers::Compressed, mip_level_size) : R_ImageScratchAlloc(
			&s_imageScratch.compress,
			mip_level_size
		);

		if (!compressed_buffer) return false;
		rygCompress(compressed_buffer, mip_buffer, scaled_width, scaled_height);

		Com_Memcpy(&upload_data->buffer[write_offset], compressed_buffer, mip_level_size);
	}
	else {
		mip_level_size = scaled_width * scaled_height * bytesPerPixel;

		Com_Memcpy(&upload_data->buffer[write_offset], mip_buffer, mip_level_size);
	}

	write_offset += mip_level_size;

	miplevel = 0;

	if (mipmap) {
		while (scaled_width > 1 || scaled_height > 1) {
			if (!R_MipMap(mip_buffer, mip_buffer, scaled_width, scaled_height, context, scratch)) return false;

			scaled_width >>= 1;
			if (scaled_width < 1)
				scaled_width = 1;

			scaled_height >>= 1;
			if (scaled_height < 1)
				scaled_height = 1;

			miplevel++;

			if ((context ? context->colorMipLevels : r_colorMipLevels->integer)) {
				R_BlendOverTexture(mip_buffer, scaled_width * scaled_height, miplevel);
			}

			if (compressed) {
				mip_level_size = rygCompressedSize(scaled_width, scaled_height);

				rygCompress(compressed_buffer, mip_buffer, scaled_width, scaled_height);

				Com_Memcpy(&upload_data->buffer[write_offset], compressed_buffer, mip_level_size);
			}
			else {
				mip_level_size = scaled_width * scaled_height * bytesPerPixel;

				Com_Memcpy(&upload_data->buffer[write_offset], mip_buffer, mip_level_size);
			}

			write_offset += mip_level_size;

			if (scaled_width == 1 && scaled_height == 1)
				break;
		}
	}

	upload_data->buffer_size = write_offset;
	upload_data->mip_levels = miplevel + 1;
    return true;
}

static VkCommandBuffer staging_command_buffer = VK_NULL_HANDLE;
static VkCommandBuffer image_upload_commands = VK_NULL_HANDLE;
static VkDeviceSize image_upload_used;

void vk_flush_image_uploads(void) {
	ImagePhaseTimer profile(vkLoadStats.imageTransferFlushUs);
	if (image_upload_commands != VK_NULL_HANDLE) {
		const VkCommandBuffer commands = image_upload_commands;
		// Clear first: the common immediate-submit helper flushes pending images.
		image_upload_commands = VK_NULL_HANDLE;
		vk_end_command_buffer(commands, "batched image upload");
		image_upload_used = 0;
	}
}

static VkDeviceSize vk_reserve_upload_bytes(VkDeviceSize size, bool batch) {
	if (!batch) vk_flush_image_uploads();
	const VkDeviceSize capacity = batch ? MAX(size, 32 * 1024 * 1024) : size;
	if (vk.staging_buffer.size < capacity) vk_alloc_staging_buffer(capacity);
	VkDeviceSize offset = PAD(image_upload_used, 16);
	if (offset > vk.staging_buffer.size || size > vk.staging_buffer.size - offset) {
		vk_flush_image_uploads();
		offset = 0;
	}
	if (image_upload_commands == VK_NULL_HANDLE) image_upload_commands = vk_begin_command_buffer();
	image_upload_used = offset + size;
	return offset;
}

void vk_upload_buffer_data(VkBuffer destination, VkDeviceSize destinationOffset, const byte *data, VkDeviceSize size) {
	const bool batch = r_imageUploadBatch && r_imageUploadBatch->integer;
	++vkLoadStats.bufferUploads;
	vkLoadStats.bufferUploadBytes += size;
	while (size) {
		const VkDeviceSize bytes = MIN(size, 32 * 1024 * 1024);
		const VkDeviceSize offset = vk_reserve_upload_bytes(bytes, batch);
		Com_Memcpy(vk.staging_buffer.ptr + offset, data, size_t(bytes));
		VkBufferCopy copy = {offset, destinationOffset, bytes};
		qvkCmdCopyBuffer(image_upload_commands, vk.staging_buffer.handle, destination, 1, &copy);
		VkBufferMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.buffer = destination; barrier.offset = destinationOffset; barrier.size = bytes;
		VK_BufferBarrier(image_upload_commands, barrier, qvkCmdPipelineBarrier, vk.synchronization2 ? qvkCmdPipelineBarrier2 : nullptr);
		data += size_t(bytes); destinationOffset += bytes; size -= bytes;
		if (!batch) vk_flush_image_uploads();
	}
}

void vk_clean_staging_buffer( void )
{
	vk_flush_image_uploads();
	if ( vk.staging_buffer.handle != VK_NULL_HANDLE ) {
		VK_DESTROY_BUFFER( vk.device, vk.staging_buffer.handle );
		vk.staging_buffer.handle = VK_NULL_HANDLE;
	}
	if ( vk.staging_buffer.memory != VK_NULL_HANDLE ) {
		VK_FREE_MEMORY( vk.device, vk.staging_buffer.memory );
		vk.staging_buffer.memory = VK_NULL_HANDLE;
	}

	vk.staging_buffer.ptr = NULL;
	vk.staging_buffer.size = 0;
#ifdef USE_UPLOAD_QUEUE
	vk.staging_buffer.offset = 0;
#endif
}

#ifdef USE_UPLOAD_QUEUE
static qboolean vk_wait_staging_buffer( void )
{
	if ( vk.aux_fence_wait ) {
		VkResult res = qvkWaitForFences( vk.device, 1, &vk.aux_fence, VK_TRUE, 5 * 1000000000ULL );

		if ( res != VK_SUCCESS ) {
			ri.Error( ERR_FATAL, "vkWaitForFences() failed with %s at %s", vk_result_string( res ), __func__ );
		}
		qvkResetFences( vk.device, 1, &vk.aux_fence );
		VK_CHECK( qvkResetCommandBuffer( vk.staging_command_buffer, 0 ) );
		vk.staging_buffer.offset = 0; // FIXME: is this correct?
		vk.aux_fence_wait = qfalse;
		return qtrue;
	} else {
		return qfalse;
	}
}


void vk_flush_staging_buffer( qboolean final )
{
	const VkPipelineStageFlags wait_dst_stage_mask = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	VkSemaphore waits;
	VkSubmitInfo submit_info;
	VkResult res;

	if ( vk.staging_buffer.offset == 0 ) {
		return;
	}

	//ri.Printf( PRINT_WARNING, S_COLOR_CYAN ">>> flush %i bytes (final=%i)<<<\n", (int)vk.staging_buffer.offset, final );

	vk.staging_buffer.offset = 0;

	VK_CHECK( qvkEndCommandBuffer( vk.staging_command_buffer ) );

	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.pNext = NULL;

	if ( vk.rendering_finished != VK_NULL_HANDLE ) {
		// first call after previous queue submission?
		waits = vk.rendering_finished;
		vk.rendering_finished = VK_NULL_HANDLE;
		submit_info.waitSemaphoreCount = 1;
		submit_info.pWaitSemaphores = &waits;
		submit_info.pWaitDstStageMask = &wait_dst_stage_mask;
	} else {
		submit_info.waitSemaphoreCount = 0;
		submit_info.pWaitSemaphores = NULL;
		submit_info.pWaitDstStageMask = NULL;
	}

	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &vk.staging_command_buffer;

	if ( vk.image_uploaded != VK_NULL_HANDLE ) {
		ri.Error( ERR_FATAL, "Vulkan: incorrect state during image upload" );
	}
	if ( final ) {
		// final submission before recording
		submit_info.signalSemaphoreCount = 1;
		submit_info.pSignalSemaphores = &vk.image_uploaded2;
		vk.image_uploaded = vk.image_uploaded2;
		VK_CHECK( vk_queue_submit( vk.queue, 1, &submit_info, vk.aux_fence ) );
		vk.aux_fence_wait = qtrue;
	} else {
		// if submission before another upload then do explicit wait
		submit_info.signalSemaphoreCount = 0;
		submit_info.pSignalSemaphores = NULL;
		VK_CHECK( vk_queue_submit( vk.queue, 1, &submit_info, vk.aux_fence ) );
		res = qvkWaitForFences( vk.device, 1, &vk.aux_fence, VK_TRUE, 5 * 1000000000ULL );
		if ( res != VK_SUCCESS ) {
			ri.Error( ERR_FATAL, "vkWaitForFences() failed with %s at %s", vk_result_string( res ), __func__ );
		}
		qvkResetFences( vk.device, 1, &vk.aux_fence );
		VK_CHECK( qvkResetCommandBuffer( vk.staging_command_buffer, 0 ) );
	}
}
#endif // USE_UPLOAD_QUEUE

void vk_alloc_staging_buffer( VkDeviceSize size )
{
	VkBufferCreateInfo buffer_desc;
	VkMemoryRequirements memory_requirements;
	VkMemoryAllocateInfo alloc_info;
	uint32_t memory_type;
	void *data;

	vk_clean_staging_buffer();

	vk.staging_buffer.size = MAX( size, STAGING_BUFFER_SIZE  );
	vk.staging_buffer.size = PAD( vk.staging_buffer.size, 1024 * 1024 );

	buffer_desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_desc.pNext = NULL;
	buffer_desc.flags = 0;
	buffer_desc.size = vk.staging_buffer.size;
	buffer_desc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	buffer_desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	buffer_desc.queueFamilyIndexCount = 0;
	buffer_desc.pQueueFamilyIndices = NULL;
	VK_CREATE_BUFFER(vk.device, &buffer_desc, &vk.staging_buffer.handle, "main staging buffer" );

	qvkGetBufferMemoryRequirements( vk.device, vk.staging_buffer.handle, &memory_requirements );

	memory_type = vk_find_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.allocationSize = memory_requirements.size;
	alloc_info.memoryTypeIndex = memory_type;

    VK_ALLOCATE_MEMORY_CHECK(vk.device, &alloc_info, &vk.staging_buffer.memory, "main staging memory");
	VK_CHECK(qvkBindBufferMemory(vk.device, vk.staging_buffer.handle, vk.staging_buffer.memory, 0));

	VK_CHECK(qvkMapMemory(vk.device, vk.staging_buffer.memory, 0, VK_WHOLE_SIZE, 0, &data));
	vk.staging_buffer.ptr = (byte*)data;
#ifdef USE_UPLOAD_QUEUE
	vk.staging_buffer.offset = 0;
#endif
	VK_SET_OBJECT_NAME(vk.staging_buffer.handle, "staging buffer", VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT);
	VK_SET_OBJECT_NAME(vk.staging_buffer.memory, "staging buffer memory", VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT);
}

static byte *vk_resample_image_data( const int target_format, byte *data, const int data_size, int *bytes_per_pixel ) {
	byte		*buffer;
	uint16_t	*p;
	int			i, n;

	switch ( target_format ) {
	case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
		buffer = R_ImageScratchAlloc( &s_imageScratch.convert, data_size / 2 );
		p = (uint16_t*)buffer;
		for ( i = 0; i < data_size; i += 4, p++ ) {
			byte r = data[i + 0];
			byte g = data[i + 1];
			byte b = data[i + 2];
			byte a = data[i + 3];
			*p = (uint32_t)((a / 255.0) * 15.0 + 0.5) |
				((uint32_t)((r / 255.0) * 15.0 + 0.5) << 4) |
				((uint32_t)((g / 255.0) * 15.0 + 0.5) << 8) |
				((uint32_t)((b / 255.0) * 15.0 + 0.5) << 12);
		}
		*bytes_per_pixel = 2;
		return buffer; // must be freed after upload!

	case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
		buffer = R_ImageScratchAlloc( &s_imageScratch.convert, data_size / 2 );
		p = (uint16_t*)buffer;
		for ( i = 0; i < data_size; i += 4, p++ ) {
			byte r = data[i + 0];
			byte g = data[i + 1];
			byte b = data[i + 2];
			*p = (uint32_t)((b / 255.0) * 31.0 + 0.5) |
				((uint32_t)((g / 255.0) * 31.0 + 0.5) << 5) |
				((uint32_t)((r / 255.0) * 31.0 + 0.5) << 10) |
				(1 << 15);
		}
		*bytes_per_pixel = 2;
		return buffer; // must be freed after upload!

	case VK_FORMAT_B8G8R8A8_UNORM:
		buffer = R_ImageScratchAlloc(&s_imageScratch.convert, data_size);
		for ( i = 0; i < data_size; i += 4 ) {
			buffer[i + 0] = data[i + 2];
			buffer[i + 1] = data[i + 1];
			buffer[i + 2] = data[i + 0];
			buffer[i + 3] = data[i + 3];
		}
		*bytes_per_pixel = 4;
		return buffer;

	case VK_FORMAT_R8G8B8_UNORM: {
		buffer = R_ImageScratchAlloc(&s_imageScratch.convert, (data_size / 4) * 3);
		for ( i = 0, n = 0; i < data_size; i += 4, n += 3 ) {
			buffer[n + 0] = data[i + 0];
			buffer[n + 1] = data[i + 1];
			buffer[n + 2] = data[i + 2];
		}
		*bytes_per_pixel = 3;
		return buffer;
	}

	default:
		*bytes_per_pixel = 4;
		return data;
	}
}

//static int batch_by_format = 0;

void vk_upload_image_data( image_t *image, int x, int y, int width,
	int height, int mipmaps, byte *pixels, int size, qboolean update )
{
	ImagePhaseTimer profile(vkLoadStats.imageUploadRecordUs, &vkLoadStats.imageTransferFlushUs);
	if (update) R_InvalidateResidentTexture(image);
	VkCommandBuffer command_buffer;
	VkBufferImageCopy regions[16];
	VkBufferImageCopy region;

	byte *buf;
	int n, mip_level_size;
	qboolean compressed = qfalse;

	int num_regions = 0;
	int buffer_size = 0;

	if ( image->internalFormat == VK_FORMAT_BC3_UNORM_BLOCK ) {
		compressed = qtrue;
		buf = pixels;
	}
	else {
		buf = vk_resample_image_data( image->internalFormat, pixels, size, &n /*bpp*/ );
	}

	//if ( batch_by_format != image->internalFormat )
	//{
	//	vk_flush_staging_command_buffer();
	//	batch_by_format = image->internalFormat;
	//}

	while (qtrue) {
		Com_Memset(&region, 0, sizeof(region));
		region.bufferOffset = buffer_size;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = num_regions;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset.x = x;
		region.imageOffset.y = y;
		region.imageOffset.z = 0;
		region.imageExtent.width = width;
		region.imageExtent.height = height;
		region.imageExtent.depth = 1;

		regions[num_regions] = region;
		num_regions++;

		if ( compressed )
			mip_level_size = rygCompressedSize(width, height);
		else
			mip_level_size = width * height * n;

		buffer_size += mip_level_size;

		if ( num_regions >= mipmaps || (width == 1 && height == 1) || num_regions >= ARRAY_LEN( regions ) )
			break;

		x >>= 1;
		y >>= 1;

		width >>= 1;
		if (width < 1) width = 1;

		height >>= 1;
		if (height < 1) height = 1;
	}
	vkLoadStats.uploadBytes += buffer_size;
	if (!update && image->hostTransfer && vk.hostImageCopy) {
		const int started = ri.Milliseconds();
		const VkResult result = VK_UploadHostImage(vk.device, image->handle, image->mipLevels, regions,
			num_regions, buf, buffer_size, qvkTransitionImageLayout, qvkCopyMemoryToImage);
		vkLoadStats.hostImageCopyMs += ri.Milliseconds() - started;
		if (result == VK_SUCCESS) {
			++vkLoadStats.hostImageCopies;
			vkLoadStats.hostImageCopyBytes += buffer_size;
			return;
		}
		// No submitted GPU command references this new image. Its contents may
		// safely be discarded and populated by the existing staged upload path.
		vk.hostImageCopy = qfalse;
		ri.Printf(PRINT_WARNING, "Vulkan host image copy disabled after %s; using staging uploads\n", vk_result_string(result));
	}

#ifdef USE_UPLOAD_QUEUE
	if ( vk_wait_staging_buffer() ) {
		// wait for vkQueueSubmit() completion before new upload
	}

	if ( compressed ) {
		vk.staging_buffer.offset = PAD(vk.staging_buffer.offset, 16);
	}

	if ( vk.staging_buffer.size - vk.staging_buffer.offset < buffer_size ) {
		// try to flush staging buffer and reset offset
		vk_flush_staging_buffer( qfalse );
	}

	if ( vk.staging_buffer.size /* - vk_world.staging_buffer_offset */ < buffer_size ) {
		// if still not enough - reallocate staging buffer
		vk_alloc_staging_buffer( buffer_size );
	}

	for ( n = 0; n < num_regions; n++ ) {
		regions[n].bufferOffset += vk.staging_buffer.offset;
	}

	Com_Memcpy( vk.staging_buffer.ptr + vk.staging_buffer.offset, buf, buffer_size );

	if ( vk.staging_buffer.offset == 0 ) {
		VkCommandBufferBeginInfo begin_info;
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.pNext = NULL;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		begin_info.pInheritanceInfo = NULL;
		VK_CHECK( qvkBeginCommandBuffer( vk.staging_command_buffer, &begin_info ) );
	}

	//ri.Printf( PRINT_WARNING, "batch @%6i + %i %s \n", (int)vk.staging_buffer.offset, (int)buffer_size, image->imgName );
	vk.staging_buffer.offset += buffer_size;

	command_buffer = vk.staging_command_buffer;

	if ( update ) {
		vk_record_image_layout_transition( command_buffer, image->handle, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 0 );
	} else {
		vk_record_image_layout_transition( command_buffer, image->handle, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_HOST_BIT, 0 );
	}

	qvkCmdCopyBufferToImage( command_buffer, vk.staging_buffer.handle, image->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, num_regions, regions );

	// final transition after upload comleted
	vk_record_image_layout_transition( command_buffer, image->handle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 0 );
#else
	const bool batch = r_imageUploadBatch && r_imageUploadBatch->integer;
	const VkDeviceSize offset = vk_reserve_upload_bytes(buffer_size, batch);
	Com_Memcpy(vk.staging_buffer.ptr + offset, buf, buffer_size);
	for (int regionIndex = 0; regionIndex < num_regions; ++regionIndex)
		regions[regionIndex].bufferOffset += offset;
	command_buffer = image_upload_commands;
	// record_buffer_memory_barrier( command_buffer, vk_world.staging_buffer, VK_WHOLE_SIZE, 0, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT );
	if ( update ) {
		vk_record_image_layout_transition( command_buffer, image->handle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 0 );
	} else {
		vk_record_image_layout_transition( command_buffer, image->handle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_HOST_BIT, 0 );
	}
	qvkCmdCopyBufferToImage( command_buffer, vk.staging_buffer.handle, image->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, num_regions, regions );
	vk_record_image_layout_transition( command_buffer, image->handle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 0 );
	if (!batch) vk_flush_image_uploads();
#endif

	// this is not ok?
	if ( buf != pixels ) {
		//ri.Hunk_FreeTempMemory( buf );
	}
}

static void allocate_and_bind_image_memory( image_t *image ) {
	VkMemoryRequirements memory_requirements;
	VkDeviceSize		alignment, size;
	ImageChunk_t		*chunk;
	int i;

	qvkGetImageMemoryRequirements(vk.device, image->handle, &memory_requirements);
	if (image->residentSourceBytes && memory_requirements.size <= residentTextures.limit() &&
		image->residentSourceBytes <= residentTextures.limit() - size_t(memory_requirements.size)) {
		std::shared_ptr<ResidentImagePage> page;
		size_t offset = 0;
		do {
			for (auto it = residentImagePages.begin(); it != residentImagePages.end();) {
				auto candidate = it->lock();
				if (!candidate) { it = residentImagePages.erase(it); continue; }
				if ((memory_requirements.memoryTypeBits & (1u << candidate->memoryType)) &&
					candidate->ranges.allocate(size_t(memory_requirements.size), size_t(memory_requirements.alignment), offset)) {
					page = candidate; break;
				}
				++it;
			}
			if (page) break;
			const size_t pageSize = size_t(std::max<VkDeviceSize>(IMAGE_CHUNK_SIZE, memory_requirements.size));
			if (residentPageCount < residentTextureLimit && pageSize <= residentTextures.limit() &&
				residentPageBytes <= residentTextures.limit() - pageSize) {
				VkMemoryAllocateInfo allocation = {};
				allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				allocation.allocationSize = pageSize;
				allocation.memoryTypeIndex = vk_find_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				VkDeviceMemory memory = VK_NULL_HANDLE;
				if (VK_ALLOCATE_MEMORY(vk.device, &allocation, &memory, "resident texture page") == VK_SUCCESS) {
					page = std::make_shared<ResidentImagePage>(memory, pageSize, allocation.memoryTypeIndex);
					residentImagePages.push_back(page);
					const bool fits = page->ranges.allocate(size_t(memory_requirements.size), size_t(memory_requirements.alignment), offset);
					assert(fits); (void)fits;
					break;
				}
			}
		} while (residentTextures.evictIdle());
		if (page) {
			auto resource = std::make_shared<ResidentTexture>(image, size_t(memory_requirements.size), page, offset);
			activeResidentTextures.emplace(image, resource);
			resource->image = image->handle;
			image->residentMemory = page->memory;
			VK_CHECK(qvkBindImageMemory(vk.device, image->handle, image->residentMemory, offset));
			return;
		}
	}

	// allow up to double the chunk size, if required
	if ( memory_requirements.size > (vk.image_chunk_size * 2) ) {
		Com_Error(ERR_DROP, "Vulkan: could not allocate memory, image is too large (%ikbytes).",
			(int)(memory_requirements.size / 1024));
	}

	chunk = VK_NULL_HANDLE;

	// Try to find an existing chunk of sufficient capacity.
	alignment = memory_requirements.alignment;
	for (i = 0; i < vk_world.num_image_chunks; i++) {
		// ensure that memory region has proper alignment
		VkDeviceSize offset = PAD(vk_world.image_chunks[i].used, alignment);

		if ((memory_requirements.memoryTypeBits & (1u << vk_world.image_chunks[i].memoryType)) &&
			offset + memory_requirements.size <= vk_world.image_chunks[i].size) {
			chunk = &vk_world.image_chunks[i];
			chunk->used = offset + memory_requirements.size;
			chunk->items++;
			break;
		}
	}

	// Allocate a new chunk in case we couldn't find suitable existing chunk.
	if (chunk == NULL) {
		VkMemoryAllocateInfo alloc_info;
		VkDeviceMemory memory;
		VkResult result;

		if (vk_world.num_image_chunks >= MAX_IMAGE_CHUNKS) {
			Com_Error(ERR_DROP, "Image chunk limit has been reached");
			vk_restart_swapchain( __func__ );
		}

		// default size is sufficient or create larger chunk
		if ( memory_requirements.size <= vk.image_chunk_size )
			size = vk.image_chunk_size;
		else {
			size = (vk.image_chunk_size * 2);

			ri.Printf(PRINT_DEVELOPER, "Vulkan: create new large memory chunk for image with size (%ikbytes)\n",
				(int)(memory_requirements.size / 1024));
		}

		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.pNext = NULL;
		alloc_info.allocationSize = size;
		alloc_info.memoryTypeIndex = vk_find_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		result = VK_ALLOCATE_MEMORY(vk.device, &alloc_info, &memory, "imag memory" );

		if (result < 0) {
			ri.Error(ERR_DROP, "%s", va("GPU memory heap overflow: Code %i", result));
			vk_restart_swapchain( __func__ );
		}

		chunk = &vk_world.image_chunks[vk_world.num_image_chunks];
		chunk->memory = memory;
		chunk->used = memory_requirements.size;
		chunk->size = size;
		chunk->items = 1;
		chunk->memoryType = alloc_info.memoryTypeIndex;

		VK_SET_OBJECT_NAME(memory, va("image memory chunk %i", vk_world.num_image_chunks), VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT);

		vk_world.num_image_chunks++;
	}

	VK_CHECK(qvkBindImageMemory(vk.device, image->handle, chunk->memory, chunk->used - memory_requirements.size));
}

void vk_update_descriptor_set( image_t *image, qboolean mipmap ) {
	Vk_Sampler_Def sampler_def;
	VkDescriptorImageInfo image_info;
	VkWriteDescriptorSet descriptor_write;

	Com_Memset(&sampler_def, 0, sizeof(sampler_def));

	sampler_def.address_mode = image->wrapClampMode;

	if (mipmap) {
		sampler_def.gl_mag_filter = gl_filter_max;
		sampler_def.gl_min_filter = gl_filter_min;
	}
	else {
		sampler_def.gl_mag_filter = GL_LINEAR;
		sampler_def.gl_min_filter = GL_LINEAR;
		// no anisotropy without mipmaps
		sampler_def.noAnisotropy = qtrue;
	}

	image_info.sampler = vk_find_sampler(&sampler_def);
	image_info.imageView = image->view;
	image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptor_write.dstSet = image->descriptor_set;
	descriptor_write.dstBinding = 0;
	descriptor_write.dstArrayElement = 0;
	descriptor_write.descriptorCount = 1;
	descriptor_write.pNext = NULL;
	descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptor_write.pImageInfo = &image_info;
	descriptor_write.pBufferInfo = NULL;
	descriptor_write.pTexelBufferView = NULL;

	qvkUpdateDescriptorSets(vk.device, 1, &descriptor_write, 0, NULL);
}

static void vk_assign_image_descriptor(image_t *image, int mip_levels) {
	if (!vk.active) return;
	if (image->descriptor_set == VK_NULL_HANDLE) {
		VkDescriptorSetAllocateInfo descriptor = {};
		descriptor.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descriptor.descriptorPool = vk.descriptor_pool;
		descriptor.descriptorSetCount = 1;
		descriptor.pSetLayouts = &vk.set_layout_sampler;
		VK_CHECK(qvkAllocateDescriptorSets(vk.device, &descriptor, &image->descriptor_set));
	}
	vk_update_descriptor_set(image, mip_levels > 1 ? qtrue : qfalse);
}

void vk_create_image( image_t *image, int width, int height, int mip_levels ) {
	ImagePhaseTimer profile(vkLoadStats.imageGpuCreateUs);
	VkFormat format = (VkFormat)image->internalFormat;
	// A resized cinematic can still be sampled by an earlier submitted frame.
	// Flush pending transfers and finish those users before replacing either
	// ordinary or resident image resources; fresh creation needs no wait.
	if (image->handle || image->view) {
		vk_flush_image_uploads();
		const VkResult idle = qvkDeviceWaitIdle(vk.device);
		if (idle != VK_SUCCESS) {
			ri.Error(ERR_DROP, "vkDeviceWaitIdle before image replacement failed: %s", vk_result_string(idle));
			return;
		}
	}
	if (image->residentMemory) {
		R_InvalidateResidentTexture(image);
		activeResidentTextures.erase(image);
		image->handle = VK_NULL_HANDLE;
		image->view = VK_NULL_HANDLE;
		image->residentMemory = VK_NULL_HANDLE;
		image->residentSourceBytes = 0;
	}
	image->mipLevels = mip_levels;

	if ( image->handle ) {
		qvkDestroyImage( vk.device, image->handle, NULL );
		image->handle = VK_NULL_HANDLE;
	}

	if ( image->view ) {
		qvkDestroyImageView( vk.device, image->view, NULL );
		image->view = VK_NULL_HANDLE;
	}
	// create image
	{
		VkImageCreateInfo desc;
		desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		desc.pNext = NULL;
		desc.flags = 0;
		desc.imageType = VK_IMAGE_TYPE_2D;
		desc.format = format;
		desc.extent.width = width;
		desc.extent.height = height;
		desc.extent.depth = 1;
		desc.mipLevels = mip_levels;
		desc.arrayLayers = 1;
		desc.samples = VK_SAMPLE_COUNT_1_BIT;
		desc.tiling = VK_IMAGE_TILING_OPTIMAL;
		desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		if (image->flags & IMGFLAG_TRANSFER_SRC) desc.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		desc.queueFamilyIndexCount = 0;
		desc.pQueueFamilyIndices = NULL;
		desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image->hostTransfer = vk.hostImageCopy && VK_CanHostCopyImage(vk.physical_device, desc,
			qvkGetPhysicalDeviceImageFormatProperties2) ? qtrue : qfalse;
		if (image->hostTransfer) desc.usage |= VK_IMAGE_USAGE_HOST_TRANSFER_BIT;
		VK_CHECK( qvkCreateImage( vk.device, &desc, NULL, &image->handle ) );

		allocate_and_bind_image_memory( image );
	}

	// create image view
	{
		VkImageViewCreateInfo desc;
		desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		desc.pNext = NULL;
		desc.flags = 0;
		desc.image = image->handle;
		desc.viewType = VK_IMAGE_VIEW_TYPE_2D;
		desc.format = format;
		desc.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		desc.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		desc.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		desc.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		desc.subresourceRange.baseMipLevel = 0;
		desc.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
		desc.subresourceRange.baseArrayLayer = 0;
		desc.subresourceRange.layerCount = 1;
		VK_CHECK( qvkCreateImageView( vk.device, &desc, NULL, &image->view ) );
	}
	const auto resident = activeResidentTextures.find(image);
	if (resident != activeResidentTextures.end()) resident->second->view = image->view;

	vk_assign_image_descriptor(image, mip_levels);

	VK_SET_OBJECT_NAME( image->handle, image->imgName, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );
	VK_SET_OBJECT_NAME( image->view, image->imgName, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT );
	VK_SET_OBJECT_NAME( image->descriptor_set, image->imgName, VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT );
}

static void vk_destroy_image_resources( VkImage *image, VkImageView *imageView )
{
	if ( image != NULL ) {
		if ( *image != VK_NULL_HANDLE ) {
			qvkDestroyImage( vk.device, *image, NULL );
			*image = VK_NULL_HANDLE;
		}
	}
	if ( imageView != NULL ) {
		if ( *imageView != VK_NULL_HANDLE ) {
			qvkDestroyImageView( vk.device, *imageView, NULL );
			*imageView = VK_NULL_HANDLE;
		}
	}
}


void vk_delete_textures( void ) {
	R_FlushTextureJobs(qtrue);
	int i;

	vk_wait_idle();

	if ( tr.images.count == 0 ) {
		return;
	}

	for (i = 0; i < tr.images.count; i++) {
		image_t *img = tr.images.items[i];
		const auto resident = activeResidentTextures.find(img);
		if (resident != activeResidentTextures.end()) {
			activeResidentTextures.erase(resident);
		} else {
			vk_destroy_image_resources(&img->handle, &img->view);
			assert(img->residentMemory == VK_NULL_HANDLE);
		}

		// img->descriptor will be released with pool reset
	}

	R_DestroyImagesPool();

	Com_Memset(tr.scratchImage, 0, sizeof(tr.scratchImage));
	Com_Memset(glState.currenttextures, 0, sizeof(glState.currenttextures));
}

static image_t *R_AllocImage( const char *name, int width, int height, imgFlags_t flags,
    const TexturePreparationContext *context = NULL ){
    image_t				*image;
    int					namelen;
    long				hash;

    namelen = (int)strlen(name) + 1;
    if (namelen > MAX_QPATH) {
        ri.Error(ERR_DROP, "R_CreateImage: \"%s\" is too long", name);
    }

#if 0
	image = noLoadImage(name, flags);
	if (image) {
		return image;
	}
#endif

    //image = (image_t*)Z_Malloc(sizeof(*image) + namelen + namelen2, TAG_IMAGE_T, qtrue);
    //image = (image_t*)ri.Hunk_Alloc(sizeof(*image) + namelen, h_low);
	image = (image_t*)Z_Malloc(sizeof(*image) + namelen, TAG_IMAGE_T);
	Com_Memset(image, 0, sizeof(*image) + namelen);

    image->imgName = (char*)(image + 1);
    strcpy(image->imgName, name);

    hash = generateHashValue(name);
    image->next = hashTable[hash];
    hashTable[hash] = image;

    R_AddImageToPool(image);

    image->flags = flags;
    image->width = width;
    image->height = height;

    if (namelen > 6 && Q_stristr(image->imgName, "maps/") == image->imgName && Q_stristr(image->imgName + 6, "/lm_") != NULL) {
        // external lightmap atlases stored in maps/<mapname>/lm_XXXX textures
        //image->flags = IMGFLAG_NOLIGHTSCALE | IMGFLAG_NO_COMPRESSION | IMGFLAG_NOSCALE | IMGFLAG_COLORSHIFT;
		image->flags |= IMGFLAG_NO_COMPRESSION | IMGFLAG_NOSCALE;
    }

    if (flags & IMGFLAG_CLAMPTOBORDER)
        image->wrapClampMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    else if (flags & IMGFLAG_CLAMPTOEDGE)
        image->wrapClampMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    else
        image->wrapClampMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;

	if (r_smartpicmip && (context ? context->smartPicmip : r_smartpicmip->integer) && Q_stricmpn(name, "textures/", 9)) {
		image->flags &= ~(IMGFLAG_PICMIP);
	}

	image->handle = VK_NULL_HANDLE;
	image->view = VK_NULL_HANDLE;
	image->descriptor_set = VK_NULL_HANDLE;



#if 0
	const char *psNewName = GenerateImageMappingName(name);
	//Q_strncpyz(image->imgName, psNewName, sizeof(image->imgName));
	R_Add_AllocatedImage(image);
#endif

    return image;
}
image_t *R_CreateImage(const char *name, byte *pic, int width, int height, imgFlags_t flags, size_t residentSourceBytes) {
	image_t *image = R_AllocImage(name, width, height, flags);
	image->residentSourceBytes = residentSourceBytes;
	vk_upload_image(image, pic);
	return image;
}

static PreparedTextures::Cache preparedTextureCache;
static TextureWorkers textureWorkers;
static bool textureJobSettingsReady, textureDiskCacheEnabled, textureWorkerPreparation;
static bool textureRegistrationActive, textureBackendExecuting;
static unsigned textureWorkerCount;
static uint64_t textureWorkerJobs;
static int textureWorkerWaitMs;
static std::atomic<uint64_t> textureWorkerDecodeUs{0}, textureWorkerCacheUs{0}, textureWorkerPrepareUs{0};
static std::atomic<unsigned> texturePreparedCount{0}, texturePreparationFallbacks{0};
static std::atomic<uint64_t> textureInlineDecodeUs{0}, textureInlineCacheUs{0};

static void R_InitTextureJobs() {
    if (textureJobSettingsReady) return;
    textureJobSettingsReady = true;
    textureWorkerDecodeUs.store(0); textureWorkerCacheUs.store(0); textureWorkerPrepareUs.store(0);
    texturePreparedCount.store(0); texturePreparationFallbacks.store(0);
    textureInlineDecodeUs.store(0); textureInlineCacheUs.store(0);
    cvar_t *workers = ri.Cvar_Get("r_textureWorkers", "-1", CVAR_ARCHIVE, "Texture decode workers (-1 automatic, 0 synchronous)");
    cvar_t *disk = ri.Cvar_Get("r_textureDiskCacheMegs", "0", CVAR_ARCHIVE, "Experimental prepared texture disk cache in MiB (0 disabled)");
    cvar_t *preparation = ri.Cvar_Get("r_textureWorkerPrepare", "1", CVAR_ARCHIVE, "Prepare texture mip chains on decode workers");
    ri.Cvar_CheckRange(preparation, 0, 1, qtrue);
    textureWorkerPreparation = preparation->integer != 0;
    // stb_dxt has lazy mutable lookup tables. Publish their completed main-thread
    // initialization before creating workers; compression then only reads them.
    static bool compressionReady = false;
    if (!compressionReady) {
        byte pixels[64] = {}, block[16];
        stb_compress_dxt_block(block, pixels, 1, STB_DXT_NORMAL);
        compressionReady = true;
    }
    ri.Cvar_CheckRange(workers, -1, 4, qtrue);
    ri.Cvar_CheckRange(disk, 0, sizeof(void *) == 8 ? 4096 : 1024, qtrue);
    textureWorkerCount = workers->integer < 0 ? std::min(4u, std::thread::hardware_concurrency() / 2) : unsigned(workers->integer);
    textureDiskCacheEnabled = false;
    if (!disk->integer || !ri.FS_GetWritePath) { preparedTextureCache.configure("", 0, 0); return; }
    const char *marker = "cache/vulkan/textures-v1/index.dat";
    char path[MAX_OSPATH];
    if (!ri.FS_GetWritePath(marker, path, sizeof(path))) return;
    fileHandle_t file = ri.FS_FOpenFileWrite(marker, qfalse);
    if (!file) return;
    ri.FS_FCloseFile(file);
    char *slash = strrchr(path, '/'), *backslash = strrchr(path, '\\');
    if (!slash || (backslash && backslash > slash)) slash = backslash;
    if (!slash) return;
    *slash = 0;
    textureDiskCacheEnabled = preparedTextureCache.configure(path, size_t(disk->integer) * 1024 * 1024,
        size_t(sizeof(void *) == 8 ? 64 : 16) * 1024 * 1024);
}
static VkFormat R_PreparedVkFormat(PreparedTextures::Format format) {
    switch (format) {
        case PreparedTextures::Format::RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
        case PreparedTextures::Format::RGBA4444: return VK_FORMAT_B4G4R4A4_UNORM_PACK16;
        case PreparedTextures::Format::RGB5551: return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
        case PreparedTextures::Format::BC3: return VK_FORMAT_BC3_UNORM_BLOCK;
    }
    return VK_FORMAT_UNDEFINED;
}
static PreparedTextures::Format R_PreparedFormat(int format) {
    switch (format) {
        case VK_FORMAT_B4G4R4A4_UNORM_PACK16: return PreparedTextures::Format::RGBA4444;
        case VK_FORMAT_A1R5G5B5_UNORM_PACK16: return PreparedTextures::Format::RGB5551;
        case VK_FORMAT_BC3_UNORM_BLOCK: return PreparedTextures::Format::BC3;
        default: return PreparedTextures::Format::RGBA8;
    }
}
struct TextureDecodeJob {
    image_t *image;
    EncodedImage encoded;
    DecodedImage decoded = {};
    byte *fallbackPixels = NULL; // Zone-owned; released explicitly on the main thread.
    TexturePreparationContext context = {};
    PreparedTextures::Texture prepared;
    PreparedTextures::Key diskKey = {};
    std::string diskSettings;
    uint32_t allowedFormats = 0;
    bool diskCache = false, preparedFromDisk = false, greyscaleApplied = false, prepareOnWorker = false;
    size_t preparationBudget = 0;
    imgFlags_t preparationFlags = imgFlags_t(0);
    std::vector<byte> source;
    std::string cacheKey;
    size_t bytes;
    std::mutex mutex;
    std::condition_variable complete;
    bool done = false;
    char error[256] = {};
    int decodeMs = 0;
    ~TextureDecodeJob() { std::free(decoded.pixels); }
};
static std::deque<std::shared_ptr<TextureDecodeJob>> textureJobs;
static size_t textureJobBytes;
static thread_local size_t textureDecodeLimit;
static void *R_AllocateDecodedTexture(size_t bytes) {
    return bytes <= textureDecodeLimit ? std::malloc(bytes) : NULL;
}

static void R_GreyscaleFileImage(byte *pic, int width, int height, const TexturePreparationContext &context) {
    if (!pic || !context.mapLoading || context.mapGreyScale <= 0) return;
    for (int i = 0; i < width * height; ++i, pic += 4) {
        if (context.mapGreyInteger) {
            const byte luma = LUMA(pic[0], pic[1], pic[2]);
            pic[0] = pic[1] = pic[2] = luma;
        } else {
            const float luma = LUMA(pic[0], pic[1], pic[2]);
            pic[0] = LERP(pic[0], luma, context.mapGreyScale);
            pic[1] = LERP(pic[1], luma, context.mapGreyScale);
            pic[2] = LERP(pic[2], luma, context.mapGreyScale);
        }
    }
}

static void R_FinishFileImage(image_t *image, byte *pic, std::vector<byte> source,
                             const std::string &cacheKey, const char *resolvedName,
                             TextureDecodeJob *job = NULL) {
    TexturePreparationContext immediate;
    const TexturePreparationContext *context = job ? &job->context : NULL;
    if (!context) { immediate = R_CaptureTextureContext(); context = &immediate; }
    if (!job || !job->greyscaleApplied) { ImagePhaseTimer profile(vkLoadStats.imagePrepareUs); R_GreyscaleFileImage(pic, image->width, image->height, *context); }
    const int createStarted = ri.Milliseconds();
    image->residentSourceBytes = source.size();
    if (job && !job->prepared.bytes.empty()) {
        const auto &description = job->prepared.description;
        if (job->diskCache && !job->preparedFromDisk) {
            ImagePhaseTimer profile(vkLoadStats.imageCacheEnqueueUs);
            preparedTextureCache.store(job->diskKey, description, job->prepared.bytes.data(), job->prepared.bytes.size());
        }
        image->internalFormat = R_PreparedVkFormat(description.format);
        image->uploadWidth = description.uploadWidth; image->uploadHeight = description.uploadHeight;
        vk_create_image(image, description.uploadWidth, description.uploadHeight, description.levels);
        vk_upload_image_data(image, 0, 0, description.uploadWidth, description.uploadHeight, description.levels,
            job->prepared.bytes.data(), static_cast<int>(job->prepared.bytes.size()), qfalse);
    } else {
        Image_Upload_Data upload;
        vk_generate_image_upload_data(image, pic, &upload, context);
        if (job && job->diskCache) {
            const PreparedTextures::Description description = {image->width, image->height,
                uint32_t(upload.base_level_width), uint32_t(upload.base_level_height), uint32_t(upload.mip_levels), R_PreparedFormat(image->internalFormat)};
            ImagePhaseTimer profile(vkLoadStats.imageCacheEnqueueUs);
            preparedTextureCache.store(job->diskKey, description, upload.buffer, upload.buffer_size);
        }
        image->uploadWidth = upload.base_level_width; image->uploadHeight = upload.base_level_height;
        vk_create_image(image, image->uploadWidth, image->uploadHeight, upload.mip_levels);
        vk_upload_image_data(image, 0, 0, image->uploadWidth, image->uploadHeight, upload.mip_levels, upload.buffer, upload.buffer_size, qfalse);
    }
		const auto resident = activeResidentTextures.find(image);
		if (resident != activeResidentTextures.end()) {
			resident->second->key = cacheKey;
			resident->second->sourceName = resolvedName;
			residentTextures.store(cacheKey, std::move(source), resident->second, resident->second->bytes);
		}

    vkLoadStats.imageCreateMs += ri.Milliseconds() - createStarted;
}

static void R_CommitTextureJob() {
    // Keep ownership in the global queue until upload succeeds so shutdown can
    // reclaim an interrupted commit through the renderer's error recovery path.
    TextureDecodeJob *job = textureJobs.front().get();
    {
        std::unique_lock<std::mutex> lock(job->mutex);
        const int started = ri.Milliseconds();
        job->complete.wait(lock, [job]() { return job->done; });
        textureWorkerWaitMs += ri.Milliseconds() - started;
    }
    ri.FS_FreeFile(job->encoded.data);
    job->encoded.data = NULL;
    vkLoadStats.imageDecodeMs += job->decodeMs;
    byte *pic = job->decoded.pixels;
    bool zonePixels = false;
    char resolved[MAX_QPATH];
    Q_strncpyz(resolved, job->encoded.resolvedName, sizeof(resolved));
    int width = job->decoded.width, height = job->decoded.height;
    if (!job->prepared.bytes.empty()) {
        R_FinishFileImage(job->image, NULL, std::move(job->source), job->cacheKey, resolved, job);
        textureJobBytes -= job->bytes;
        textureJobs.pop_front();
        return;
    }
    if (!pic) {
        // A valid header can still contain corrupt pixels. Retry the ordinary
        // loader so a valid alternate extension wins exactly as before.
        R_LoadImage(job->image->imgName, &pic, &width, &height, resolved);
        job->fallbackPixels = pic;
        zonePixels = true;
        job->source.clear();
        job->diskCache = false;
    }
    byte missing[16] = {255,0,255,255, 0,0,0,255, 0,0,0,255, 255,0,255,255};
    const bool validDimensions = width > 0 && height > 0 && width <= 65535 && height <= 65535 &&
        !(width & (width - 1)) && !(height & (height - 1)) && size_t(width) * height <= size_t(INT_MAX) / 4;
    if (!pic || !validDimensions || (!zonePixels &&
        (width != job->encoded.width || height != job->encoded.height))) {
        if (pic && zonePixels) { ri.Z_Free(pic); job->fallbackPixels = NULL; }
        ri.Printf(PRINT_WARNING, "Texture decode failed: %s (%s)\n", job->image->imgName, job->error);
        pic = missing; width = height = 2; zonePixels = false; job->source.clear(); job->diskCache = false;
    }
    job->image->width = width; job->image->height = height;
    R_FinishFileImage(job->image, pic, std::move(job->source), job->cacheKey, resolved, job);
    if (zonePixels) { ri.Z_Free(pic); job->fallbackPixels = NULL; }
    textureJobBytes -= job->bytes;
    textureJobs.pop_front();
}

static void R_CollectTextureProfile() {
    if (!textureJobSettingsReady) return;
    vkLoadStats.workerDecodeUs = textureWorkerDecodeUs.load(std::memory_order_relaxed);
    vkLoadStats.workerPrepareUs = textureWorkerPrepareUs.load(std::memory_order_relaxed);
    vkLoadStats.workerPreparedJobs = texturePreparedCount.load(std::memory_order_relaxed);
    vkLoadStats.workerPreparationFallbacks = texturePreparationFallbacks.load(std::memory_order_relaxed);
    vkLoadStats.workerCacheUs = textureWorkerCacheUs.load(std::memory_order_relaxed);
    vkLoadStats.inlineDecodeUs = textureInlineDecodeUs.load(std::memory_order_relaxed);
    vkLoadStats.inlineCacheUs = textureInlineCacheUs.load(std::memory_order_relaxed);
}

void R_FlushTextureJobs(qboolean cancel) {
    if (cancel) {
        textureRegistrationActive = textureBackendExecuting = false;
        textureWorkers.shutdown();
        R_CollectTextureProfile();
        for (auto &job : textureJobs) {
            if (job->encoded.data) ri.FS_FreeFile(job->encoded.data);
            if (job->fallbackPixels) ri.Z_Free(job->fallbackPixels);
        }
        textureJobs.clear(); textureJobBytes = 0;
        preparedTextureCache.shutdown();
        if (ri.Cvar_VariableIntegerValue("cl_loadTimings") && textureWorkerJobs) {
            const auto cache = preparedTextureCache.statistics();
            ri.Printf(PRINT_ALL, "LOADPROFILE texture jobs: %llu jobs, %u decode workers, %d ms main-thread waits; prepared disk cache %llu hits, %llu misses, %llu writes, %llu skipped, %llu invalid\n",
                static_cast<unsigned long long>(textureWorkerJobs), textureWorkerCount, textureWorkerWaitMs,
                static_cast<unsigned long long>(cache.hits), static_cast<unsigned long long>(cache.misses), static_cast<unsigned long long>(cache.writes),
                static_cast<unsigned long long>(cache.droppedWrites), static_cast<unsigned long long>(cache.invalid));
        }
        textureJobSettingsReady = textureDiskCacheEnabled = false;
        textureWorkerJobs = 0; textureWorkerWaitMs = 0;
        return;
    }
    while (!textureJobs.empty()) R_CommitTextureJob();
    R_CollectTextureProfile();
}

void R_SetTextureRegistration(qboolean active) {
    if (!active) R_FlushTextureJobs();
    textureRegistrationActive = active != qfalse;
}

void R_TextureBackend(qboolean executing) {
    if (executing) R_FlushTextureJobs();
    textureBackendExecuting = executing != qfalse;
}

static bool R_CanDeferTexture(const char *name, imgFlags_t flags) {
    return textureRegistrationActive && !textureBackendExecuting && !vk.frame_count && name[0] != '*' &&
        !(flags & (IMGFLAG_LIGHTMAP | IMGFLAG_COLORSHIFT | IMGFLAG_TRANSFER_SRC));
}

static image_t *R_QueueFileImage(const char *name, imgFlags_t flags,
                               std::vector<byte> &source, const std::string &cacheKey,
                               const TexturePreparationContext &context, bool prefixSignature) {
    // Defer only frontend registration. Runtime and backend-discovered images,
    // procedural lightmaps, dynamic textures and transfer sources stay immediate.
    if (!R_CanDeferTexture(name, flags)) return NULL;
    R_InitTextureJobs();
    if (!textureWorkerCount && !textureDiskCacheEnabled) return NULL;
    if (textureWorkerCount && !textureWorkers.start(textureWorkerCount)) textureWorkerCount = 0;
    if (!textureWorkerCount && !textureDiskCacheEnabled) return NULL;
    EncodedImage encoded;
    const uint64_t sourceStarted = ImageTimeUs();
    const bool foundEncoded = R_ReadEncodedImage(name, encoded);
    const uint64_t readProbeUs = ImageTimeUs() - sourceStarted;
    vkLoadStats.imageSourceUs += readProbeUs;
    vkLoadStats.imageEncodedReadProbeUs += readProbeUs;
    if (!foundEncoded) return NULL;
    if (prefixSignature && !cacheKey.empty()) {
        const uint64_t metadataStarted = ImageTimeUs();
        R_ImageSourceSignature(name, source, std::min<size_t>(residentTextures.limit(), 64 * 1024 * 1024), encoded.resolvedName);
        const uint64_t metadataUs = ImageTimeUs() - metadataStarted;
        vkLoadStats.imageSourceUs += metadataUs;
        vkLoadStats.imageSourceMetadataUs += metadataUs;
    }
    // A disk hit contains a full RGBA8 mip chain (also for 16-bit GPU formats).
    // Bound that worst case rather than only the decoded base level.
    const size_t pixels = size_t(encoded.width) * encoded.height;
    const size_t budget = (sizeof(void *) == 8 ? 128 : 32) * size_t(1024 * 1024);
    const bool prepareOnWorker = textureWorkerCount && textureWorkerPreparation;
    // Encoded input + RGBA decode + worst-case RGBA mip output + filtering,
    // compression and HQ temporary scratch. Rectangular mip chains need up to
    // twice the base pixels; reserve the peak, not only the final upload bytes.
    const size_t bytesPerPixel = prepareOnWorker ? 20 : textureDiskCacheEnabled ? 8 : 4;
    if (encoded.length > budget - 65536 || pixels > (budget - 65536 - encoded.length) / bytesPerPixel) {
        if (prepareOnWorker) ++vkLoadStats.workerPreparationOversized;
        ri.FS_FreeFile(encoded.data);
        R_FlushTextureJobs();
        return NULL;
    }
    const size_t bytes = encoded.length + pixels * bytesPerPixel + 65536;
    while (textureJobBytes > budget - bytes || textureJobs.size() >= 64) R_CommitTextureJob();
    auto job = std::make_shared<TextureDecodeJob>();
    job->image = R_AllocImage(name, encoded.width, encoded.height, flags, &context);
    job->context = context;
    job->encoded = encoded; job->source = source; job->cacheKey = cacheKey; job->bytes = bytes;
    job->diskCache = textureDiskCacheEnabled;
    job->prepareOnWorker = prepareOnWorker;
    job->preparationBudget = pixels * 16 + 65536;
    job->preparationFlags = job->image->flags;
    if (job->diskCache) {
        job->diskSettings = R_ResidentTextureKey(name, flags, context);
        job->diskSettings.append(ARCH_STRING);
        if (context.textureBits > 0 && context.textureBits <= 16)
            job->allowedFormats = (1u << uint32_t(PreparedTextures::Format::RGBA4444)) | (1u << uint32_t(PreparedTextures::Format::RGB5551));
        else job->allowedFormats = 1u << uint32_t(context.compressedFormat && !(job->image->flags & IMGFLAG_NO_COMPRESSION) ?
            PreparedTextures::Format::BC3 : PreparedTextures::Format::RGBA8);
    }
    textureJobBytes += bytes;
    textureJobs.push_back(job);
    ++textureWorkerJobs;
    ++vkLoadStats.imageLoads;
    const bool onWorker = textureWorkerCount != 0;
    const auto decode = [job, onWorker]() {
        const auto started = std::chrono::steady_clock::now();
        textureDecodeLimit = size_t(job->encoded.width) * job->encoded.height * 4;
        const ImageAllocator allocator = {R_AllocateDecodedTexture, std::free};
        bool prepared = false;
        if (job->diskCache) {
            const uint64_t cacheStarted = ImageTimeUs();
            job->diskKey = PreparedTextures::ContentKey(job->encoded.data, job->encoded.length, job->diskSettings.data(), job->diskSettings.size());
            prepared = preparedTextureCache.read(job->diskKey, job->encoded.width, job->encoded.height, job->allowedFormats, job->prepared);
            job->preparedFromDisk = prepared;
            (onWorker ? textureWorkerCacheUs : textureInlineCacheUs).fetch_add(ImageTimeUs() - cacheStarted, std::memory_order_relaxed);
        }
        const uint64_t decoderStarted = ImageTimeUs();
        if (!prepared && job->encoded.format == EncodedImageFormat::JPG)
            R_DecodeJPG(job->encoded.data, job->encoded.length, allocator, job->decoded, job->error, sizeof(job->error));
        else if (!prepared) R_DecodePNG(job->encoded.data, job->encoded.length, allocator, job->decoded, job->error, sizeof(job->error));
        if (!prepared) (onWorker ? textureWorkerDecodeUs : textureInlineDecodeUs).fetch_add(ImageTimeUs() - decoderStarted, std::memory_order_relaxed);
        if (!prepared && job->prepareOnWorker && job->decoded.pixels &&
            job->decoded.width == job->encoded.width && job->decoded.height == job->encoded.height) {
            const uint64_t preparationStarted = ImageTimeUs();
            TexturePreparationBuffers scratch(job->preparationBudget);
            image_t image = {};
            image.width = job->decoded.width; image.height = job->decoded.height;
            image.flags = job->preparationFlags;
            R_GreyscaleFileImage(job->decoded.pixels, image.width, image.height, job->context);
            job->greyscaleApplied = true;
            Image_Upload_Data upload = {};
            if (vk_generate_image_upload_data(&image, job->decoded.pixels, &upload, &job->context, &scratch)) {
                job->prepared.description = {image.width, image.height, uint32_t(upload.base_level_width),
                    uint32_t(upload.base_level_height), uint32_t(upload.mip_levels), R_PreparedFormat(image.internalFormat)};
                job->prepared.bytes = std::move(scratch.buffers[TexturePreparationBuffers::Upload]);
                std::free(job->decoded.pixels); job->decoded.pixels = NULL;
                texturePreparedCount.fetch_add(1, std::memory_order_relaxed);
            } else texturePreparationFallbacks.fetch_add(1, std::memory_order_relaxed);
            textureWorkerPrepareUs.fetch_add(ImageTimeUs() - preparationStarted, std::memory_order_relaxed);
        }

        job->decodeMs = int(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
        { std::lock_guard<std::mutex> lock(job->mutex); job->done = true; }
        job->complete.notify_one();
    };
    if (textureWorkerCount) textureWorkers.submit(decode);
    else decode();
    return job->image;
}

image_t *R_FindImageFile( const char *name, imgFlags_t flags ){
        image_t		*image;
        int			width, height;
        byte		*pic;
        int			hash;

		if (!name || ri.Cvar_VariableIntegerValue("dedicated"))	// stop ghoul2 horribleness as regards image loading from server
		{
			return NULL;
		}

        hash = generateHashValue(name);

        //
        // see if the image is already loaded
#if 0
		image = noLoadImage(name, flags);
		if (image) {
			return image;
		}
#endif
        for (image = hashTable[hash]; image; image = image->next) {
            if (!Q_stricmp(name, image->imgName)) {
                // the white image can be used with any set of parms, but other mismatches are errors
                if (strcmp(name, "*white")) {
                    if (image->flags != flags) {
                        ri.Printf(PRINT_DEVELOPER, "WARNING: reused image %s with mixed flags (%i vs %i)\n", name, image->flags, flags);
                    }
                }
                return image;
            }
        }

        //
        // load the pic from disk
        //
        const int decodeStarted = ri.Milliseconds();
        const uint64_t sourceStarted = ImageTimeUs();
        const TexturePreparationContext context = R_CaptureTextureContext();
		std::vector<byte> source;
		std::string cacheKey;
		const bool prefixSignature = textureSignaturePrefix && textureSignaturePrefix->integer != 0;
		const bool eligible = residentTextures.limit() && name[0] != '*' &&
			!(flags & (IMGFLAG_LIGHTMAP | IMGFLAG_COLORSHIFT)) && Q_stricmpn(name, "maps/", 5);
		if (eligible && (prefixSignature ||
			R_ImageSourceSignature(name, source, std::min<size_t>(residentTextures.limit(), 64 * 1024 * 1024)))) {
			cacheKey = R_ResidentTextureKey(name, flags, context);
			// Keep full and prefix signatures separate if the A/B setting changes
			// between registrations. This does not affect prepared pixel contents.
			cacheKey.push_back(prefixSignature ? '\1' : '\0');
			if (prefixSignature) {
				const auto previous = residentTextures.peek(cacheKey);
				if (previous)
					R_ImageSourceSignature(name, source, std::min<size_t>(residentTextures.limit(), 64 * 1024 * 1024), previous->sourceName.c_str());
			}
			auto resource = residentTextures.find(cacheKey, source);
			if (resource) {
				fsReadFileIdentity_t identity;
				ri.FS_GetReadFileIdentity(resource->sourceName.c_str(), &identity, qtrue);
                const uint64_t metadataUs = ImageTimeUs() - sourceStarted;
                vkLoadStats.imageSourceUs += metadataUs;
                vkLoadStats.imageSourceMetadataUs += metadataUs;
                const uint64_t reuseStarted = ImageTimeUs();
                image = R_AllocImage(name, resource->width, resource->height, flags);
				image->uploadWidth = resource->uploadWidth; image->uploadHeight = resource->uploadHeight;
				image->internalFormat = resource->format; image->mipLevels = resource->mipLevels;
				image->handle = resource->image; image->view = resource->view;
				image->residentMemory = resource->page->memory;
				activeResidentTextures.emplace(image, resource);
				vk_assign_image_descriptor(image, resource->mipLevels);
                vkLoadStats.imageGpuCreateUs += ImageTimeUs() - reuseStarted;
				vkLoadStats.imageDecodeMs += ri.Milliseconds() - decodeStarted;
				return image;
			}
		}
        const uint64_t metadataUs = ImageTimeUs() - sourceStarted;
        vkLoadStats.imageSourceUs += metadataUs;
        vkLoadStats.imageSourceMetadataUs += metadataUs;
        image = R_QueueFileImage(name, flags, source, cacheKey, context, prefixSignature);
        if (image) {
            if (!textureWorkerCount) R_FlushTextureJobs();
            vkLoadStats.imageDecodeMs += ri.Milliseconds() - decodeStarted;
            return image;
        }
		char resolvedName[MAX_QPATH];
        const uint64_t synchronousStarted = ImageTimeUs();
        R_LoadImage(name, &pic, &width, &height, resolvedName);
        vkLoadStats.imageSyncReadDecodeUs += ImageTimeUs() - synchronousStarted;
        vkLoadStats.imageDecodeMs += ri.Milliseconds() - decodeStarted;
        ++vkLoadStats.imageLoads;
        if (pic == NULL) {
            return NULL;
        }
		if (prefixSignature && !cacheKey.empty()) {
			const uint64_t metadataStarted = ImageTimeUs();
			R_ImageSourceSignature(name, source, std::min<size_t>(residentTextures.limit(), 64 * 1024 * 1024), resolvedName);
			const uint64_t metadataUs = ImageTimeUs() - metadataStarted;
			vkLoadStats.imageSourceUs += metadataUs;
			vkLoadStats.imageSourceMetadataUs += metadataUs;
		}

		// refuse to find any files not power of 2 dims...
		//
		if ((width & (width - 1)) || (height & (height - 1)))
		{
			ri.Printf(PRINT_ALL, "Refusing to load non-power-2-dims(%d,%d) pic \"%s\"...\n", width, height, name);
			ri.Z_Free(pic);
			return NULL;
		}

        image = R_AllocImage(name, width, height, flags);
        R_FinishFileImage(image, pic, std::move(source), cacheKey, resolvedName);
        ri.Z_Free(pic);
        return image;
}

void RE_UploadCinematic( int cols, int rows, const byte *data, int client, qboolean dirty )
{
	image_t *image;

    if ( !tr.scratchImage[client] ) {
		tr.scratchImage[client] = R_CreateImage(va("*scratch%i", client), (byte*)data, cols, rows,
			IMGFLAG_CLAMPTOEDGE | IMGFLAG_RGB | IMGFLAG_NOSCALE | IMGFLAG_NO_COMPRESSION);
		return;
    }

    image = tr.scratchImage[client];

    vk_bind( image );

    // if the scratchImage isn't in the format we want, specify it as a new texture
    if ( cols != image->width || rows != image->height )  {
		image->width = image->uploadWidth = cols;
		image->height = image->uploadHeight = rows;

		vk_create_image( image, cols, rows, 1 );
		vk_upload_image_data( image, 0, 0, cols, rows, 1, (byte*)data, cols * rows * 4, qfalse );

    }
    else if (dirty)
    {
        vk_upload_image_data( image, 0, 0, cols, rows, 1, (byte*)data, cols * rows * 4, qtrue );
    }
}

static int Hex( char c )
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'A' && c <= 'F') {
		return 10 + c - 'A';
	}
	if (c >= 'a' && c <= 'f') {
		return 10 + c - 'a';
	}

	return -1;
}

/*
==================
R_BuildDefaultImage

Create solid color texture from following input formats (hex):
#rgb
#rrggbb
==================
*/
static qboolean R_BuildDefaultImage( const char *format ) {
	byte data[DEFAULT_SIZE][DEFAULT_SIZE][4];
	byte color[4];
	int i, len, hex[6];
	int x, y;

	if (*format++ != '#') {
		return qfalse;
	}

	len = (int)strlen(format);
	if (len <= 0 || len > 6) {
		return qfalse;
	}

	for (i = 0; i < len; i++) {
		hex[i] = Hex(format[i]);
		if (hex[i] == -1) {
			return qfalse;
		}
	}

	switch (len) {
	case 3: // #rgb
		color[0] = hex[0] << 4 | hex[0];
		color[1] = hex[1] << 4 | hex[1];
		color[2] = hex[2] << 4 | hex[2];
		color[3] = 255;
		break;
	case 6: // #rrggbb
		color[0] = hex[0] << 4 | hex[1];
		color[1] = hex[2] << 4 | hex[3];
		color[2] = hex[4] << 4 | hex[5];
		color[3] = 255;
		break;
	default: // unsupported format
		return qfalse;
	}

	for (y = 0; y < DEFAULT_SIZE; y++) {
		for (x = 0; x < DEFAULT_SIZE; x++) {
			data[x][y][0] = color[0];
			data[x][y][1] = color[1];
			data[x][y][2] = color[2];
			data[x][y][3] = color[3];
		}
	}

	tr.defaultImage = R_CreateImage("*default", (byte *)data, DEFAULT_SIZE, DEFAULT_SIZE, IMGFLAG_MIPMAP);

	return qtrue;
}

static void R_CreateDefaultImage( void )
{
	uint32_t x;
	byte data[DEFAULT_SIZE][DEFAULT_SIZE][4];

	vk_debug("%s \n", __func__);

	if (r_defaultImage->string[0])
	{
		// build from format
		if (R_BuildDefaultImage(r_defaultImage->string))
			return;

		// load from external file
		tr.defaultImage = R_FindImageFile(r_defaultImage->string, IMGFLAG_MIPMAP | IMGFLAG_PICMIP);
		if (tr.defaultImage)
			return;
	}

	// the default image will be a box, to allow you to see the mapping coordinates
	Com_Memset(data, 32, sizeof(data));

	for (x = 0; x < DEFAULT_SIZE; x++)
	{
		data[0][x][0] =
		data[0][x][1] =
		data[0][x][2] =
		data[0][x][3] = 255;

		data[x][0][0] =
		data[x][0][1] =
		data[x][0][2] =
		data[x][0][3] = 255;

		data[DEFAULT_SIZE - 1][x][0] =
		data[DEFAULT_SIZE - 1][x][1] =
		data[DEFAULT_SIZE - 1][x][2] =
		data[DEFAULT_SIZE - 1][x][3] = 255;

		data[x][DEFAULT_SIZE - 1][0] =
		data[x][DEFAULT_SIZE - 1][1] =
		data[x][DEFAULT_SIZE - 1][2] =
		data[x][DEFAULT_SIZE - 1][3] = 255;
	}

	tr.defaultImage = R_CreateImage("*default", (byte*)data, DEFAULT_SIZE, DEFAULT_SIZE, IMGFLAG_MIPMAP);
}

static void R_CreateDlightImage( void )
{
    int		width, height;
    byte	*pic;

	vk_debug("%s \n", __func__);

    R_LoadImage("gfx/2d/dlight", &pic, &width, &height);
    if (pic)
    {
        tr.dlightImage = R_CreateImage("*dlight", pic, width, height, IMGFLAG_CLAMPTOEDGE);
        Z_Free(pic);
    }
    else
    {	// if we dont get a successful load
        int		x, y;
        byte	data[DLIGHT_SIZE][DLIGHT_SIZE][4];
        int		b;

        // make a centered inverse-square falloff blob for dynamic lighting
        for (x = 0; x < DLIGHT_SIZE; x++) {
            for (y = 0; y < DLIGHT_SIZE; y++) {
                float	d;

                d = (DLIGHT_SIZE / 2 - 0.5f - x) * (DLIGHT_SIZE / 2 - 0.5f - x) +
                    (DLIGHT_SIZE / 2 - 0.5f - y) * (DLIGHT_SIZE / 2 - 0.5f - y);
                b = 4000 / d;
                if (b > 255) {
                    b = 255;
                }
                else if (b < 75) {
                    b = 0;
                }
                data[y][x][0] =
                    data[y][x][1] =
                    data[y][x][2] = b;
                data[y][x][3] = 255;
            }
        }
        tr.dlightImage = R_CreateImage("*dlight", (byte*)data, DLIGHT_SIZE, DLIGHT_SIZE, IMGFLAG_CLAMPTOEDGE);
    }
}

static void R_CreateFogImage( void )
{
    int		x, y;
    byte	*data;
    float	d;

	vk_debug("%s \n", __func__);

    data = (unsigned char*)Hunk_AllocateTempMemory(FOG_S * FOG_T * 4);

    // S is distance, T is depth
    for (x = 0; x < FOG_S; x++) {
        for (y = 0; y < FOG_T; y++) {
            d = R_FogFactor((x + 0.5f) / FOG_S, (y + 0.5f) / FOG_T);

            data[(y * FOG_S + x) * 4 + 0] =
            data[(y * FOG_S + x) * 4 + 1] =
            data[(y * FOG_S + x) * 4 + 2] = 255;
            data[(y * FOG_S + x) * 4 + 3] = 255 * d;
        }
    }

    tr.fogImage = R_CreateImage("*fog", data, FOG_S, FOG_T, IMGFLAG_CLAMPTOEDGE);
    ri.Hunk_FreeTempMemory(data);
}

/*
==================
R_CreateBuiltinImages
==================
*/
static void R_CreateBuiltinImages( void ) {
	int		x, y;
	byte	data[DEFAULT_SIZE][DEFAULT_SIZE][4];

	vk_debug("%s \n", __func__);

	R_CreateDefaultImage();

	// we use a solid white image instead of disabling texturing
	Com_Memset(data, 255, sizeof(data));
	tr.whiteImage = R_CreateImage("*white", (byte*)data, 8, 8, IMGFLAG_NONE);

	Com_Memset(data, 0, sizeof(data));
	tr.blackImage = R_CreateImage("*black", (byte*)data, 8, 8, IMGFLAG_NONE);

	// with overbright bits active, we need an image which is some fraction of full color,
	// for default lightmaps, etc
	for ( x = 0; x < DEFAULT_SIZE; x++ ) {
		for ( y = 0; y < DEFAULT_SIZE; y++ ) {
			data[y][x][0] =
			data[y][x][1] =
			data[y][x][2] = tr.identityLightByte;
			data[y][x][3] = 255;
		}
	}
	tr.identityLightImage = R_CreateImage("*identityLight", (byte*)data, 8, 8, IMGFLAG_NONE);

	//for ( x = 0; x < ARRAY_LEN( tr.scratchImage ); x++ ) {
		// scratchimage is usually used for cinematic drawing
		//tr.scratchImage[x] = R_CreateImage( "*scratch", (byte*)data, DEFAULT_SIZE, DEFAULT_SIZE,
		//	IMGFLAG_PICMIP | IMGFLAG_CLAMPTOEDGE | IMGFLAG_RGB );
	//}

	R_CreateDlightImage();
	R_CreateFogImage();
}

void R_InitImages( void )
{
	vk_debug("Initialize images \n");

	// initialize linear gamma table before setting color mappings for the first time
	int i;
	for ( i = 0; i < 256; i++ )
		s_gammatable_linear[i] = (unsigned char)i;

	Com_Memset(hashTable, 0, sizeof(hashTable));

    // build brightness translation tables
    R_SetColorMappings();
	R_InitResidentTextures();

	// create default texture and white texture
	R_CreateBuiltinImages();

	vk_update_post_process_pipelines();
}
