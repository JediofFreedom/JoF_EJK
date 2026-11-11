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
#include <algorithm>

void vk_restart_swapchain( const char *funcname )
{
    uint32_t i;
    ri.Printf( PRINT_WARNING, "%s(): restarting swapchain...\n", funcname );
    vk_debug( "Restarting swapchain \n" );

    vk_wait_idle();

    for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
        qvkResetCommandBuffer( vk.tess[i].command_buffer, 0 );
    }

#ifdef USE_UPLOAD_QUEUE
	qvkResetCommandBuffer( vk.staging_command_buffer, 0 );

#endif

    vk_destroy_pipelines(qfalse);
    vk_destroy_framebuffers();
    vk_destroy_render_passes();
    vk_destroy_attachments();
    vk_destroy_swapchain();
    vk_destroy_sync_primitives();

    if (!vk_select_surface_format( vk.physical_device, vk.surface ))
        ri.Error(ERR_FATAL, "Vulkan: cannot select surface format during swapchain recreation");
    vk_setup_surface_formats( vk.physical_device );

    vk_create_sync_primitives();
    vk_create_swapchain( vk.physical_device, vk.device, vk.surface, vk.present_format, &vk.swapchain );
    vk_create_attachments();
    vk_create_render_passes();
    vk_create_framebuffers();

    vk_update_attachment_descriptors();
    vk_update_post_process_pipelines();
}

static const char *vk_pmode_to_str( VkPresentModeKHR mode )
{
    static char buf[32];

    switch ( mode ) {
        case VK_PRESENT_MODE_IMMEDIATE_KHR:     return "IMMEDIATE";
        case VK_PRESENT_MODE_MAILBOX_KHR:       return "MAILBOX";
        case VK_PRESENT_MODE_FIFO_KHR:          return "FIFO";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR:  return "FIFO_RELAXED";
        case VK_PRESENT_MODE_FIFO_LATEST_READY_EXT: return "FIFO_LATEST_READY";
        default: Com_sprintf(buf, sizeof(buf), "mode#%x", mode); return buf;
    };
}

void vk_create_swapchain( VkPhysicalDevice physical_device, VkDevice device,
    VkSurfaceKHR surface, VkSurfaceFormatKHR surface_format, VkSwapchainKHR *swapchain )
{
    int                         v;
    VkImageViewCreateInfo       view;
    VkSurfaceCapabilitiesKHR    surface_caps;
    VkExtent2D                  image_extent;
    uint32_t                    present_mode_count, i, image_count;
    VkPresentModeKHR            present_mode, *present_modes;
    VkSwapchainCreateInfoKHR    desc;
    qboolean                    mailbox_supported = qfalse;
    qboolean                    immediate_supported = qfalse;
    qboolean                    fifo_relaxed_supported = qfalse;


    VK_REQUIRE_SUCCESS( qvkGetPhysicalDeviceSurfaceCapabilitiesKHR( physical_device, surface, &surface_caps ) );

    image_extent = surface_caps.currentExtent;
    if (image_extent.width == UINT32_MAX && image_extent.height == UINT32_MAX) {
        // Presentation follows the window, not an independently supersampled
        // FBO render target. Clamp only when the surface leaves size selection
        // to the application.
        image_extent.width = std::min(surface_caps.maxImageExtent.width,
            std::max(surface_caps.minImageExtent.width, uint32_t(gls.windowWidth)));
        image_extent.height = std::min(surface_caps.maxImageExtent.height,
            std::max(surface_caps.minImageExtent.height, uint32_t(gls.windowHeight)));
    }
    // Fixed currentExtent is mandatory and can legitimately change after a
    // fullscreen restore. Adapt presentation resources before creating them.
    // Minimized recreation is deferred by acquisition until restore.
    if (!image_extent.width || !image_extent.height ||
        image_extent.width < surface_caps.minImageExtent.width || image_extent.width > surface_caps.maxImageExtent.width ||
        image_extent.height < surface_caps.minImageExtent.height || image_extent.height > surface_caps.maxImageExtent.height)
        ri.Error(ERR_FATAL, "Vulkan: unsupported surface extent %u x %u", image_extent.width, image_extent.height);
    vk_apply_surface_extent(image_extent.width, image_extent.height);

    vk.clearAttachment = qtrue;
    vk.swapchainTransferDst = (surface_caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) ? qtrue : qfalse;

    if ( !vk.fboActive ) {
        // VK_IMAGE_USAGE_TRANSFER_DST_BIT is required by image clear operations.
        if ( ( surface_caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) == 0 ) {
            vk.clearAttachment = qfalse;
            ri.Printf( PRINT_WARNING, "VK_IMAGE_USAGE_TRANSFER_DST_BIT is not supported by the swapchain, \\r_clear might not work\n" );
        }

        // VK_IMAGE_USAGE_TRANSFER_SRC_BIT is required in order to take screenshots.
        if ( ( surface_caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT ) == 0 ) {
            ri.Error( ERR_FATAL, "create_swapchain: VK_IMAGE_USAGE_TRANSFER_SRC_BIT is not supported by the swapchain" );
        }
    }

    // determine present mode and swapchain image count
    VK_REQUIRE_SUCCESS( qvkGetPhysicalDeviceSurfacePresentModesKHR( physical_device, surface, &present_mode_count, NULL ) );

    present_modes = (VkPresentModeKHR*)malloc( present_mode_count * sizeof( VkPresentModeKHR ) );
    if (!present_mode_count || !present_modes)
        ri.Error(ERR_FATAL, "Vulkan: cannot allocate surface presentation modes");
    const VkResult modeResult = qvkGetPhysicalDeviceSurfacePresentModesKHR( physical_device, surface, &present_mode_count, present_modes );
    if (modeResult != VK_SUCCESS) {
        free(present_modes);
        VK_REQUIRE_SUCCESS(modeResult);
    }

    ri.Printf( PRINT_ALL, "----- Presentation modes -----\n" );

    for ( i = 0; i < present_mode_count; i++ ) {
        ri.Printf( PRINT_ALL, " %s\n", vk_pmode_to_str( present_modes[i] ) );

        switch ( present_modes[i] ) {
            case VK_PRESENT_MODE_MAILBOX_KHR: mailbox_supported = qtrue; break;
            case VK_PRESENT_MODE_IMMEDIATE_KHR: immediate_supported = qtrue; break;
            case VK_PRESENT_MODE_FIFO_RELAXED_KHR: fifo_relaxed_supported = qtrue; break;
            default: break;
        }
    }

    free( present_modes );

    if ( ( v = ri.Cvar_VariableIntegerValue( "r_swapInterval" ) ) != 0 ) {
        if ( v == 3 && mailbox_supported )
            present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
        else if ( v == 2 && fifo_relaxed_supported )
            present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        else
            present_mode = VK_PRESENT_MODE_FIFO_KHR;
        image_count = MAX( MIN_SWAPCHAIN_IMAGES_FIFO, surface_caps.minImageCount );
    }
    else {
        if ( immediate_supported ) {
            present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            image_count = MAX( MIN_SWAPCHAIN_IMAGES_IMM, surface_caps.minImageCount );
        }
        else if ( mailbox_supported ) {
            present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
            image_count = MAX( MIN_SWAPCHAIN_IMAGES_MAILBOX, surface_caps.minImageCount );
        }
        else if ( fifo_relaxed_supported ) {
            present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
            image_count = MAX( MIN_SWAPCHAIN_IMAGES_FIFO, surface_caps.minImageCount );
        }
        else {
            present_mode = VK_PRESENT_MODE_FIFO_KHR;
            image_count = MAX( MIN_SWAPCHAIN_IMAGES_FIFO, surface_caps.minImageCount );
        }
    }

    if (image_count < 2) {
        image_count = 2;
    }

    if ( surface_caps.maxImageCount == 0 && present_mode == VK_PRESENT_MODE_FIFO_KHR ) {
		image_count = MAX( MIN_SWAPCHAIN_IMAGES_FIFO_0, surface_caps.minImageCount );
	} else if ( surface_caps.maxImageCount > 0 ) {
        image_count = MIN( image_count, surface_caps.maxImageCount );
    }
    if (image_count > MAX_SWAPCHAIN_IMAGES)
        ri.Error(ERR_FATAL, "Vulkan: surface requires %u swapchain images, renderer supports %u", image_count, MAX_SWAPCHAIN_IMAGES);

    ri.Printf( PRINT_ALL, "selected presentation mode: %s, image count: %i\n", vk_pmode_to_str( present_mode ), image_count );

    // create swap chain
    desc.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    desc.pNext = NULL;
    desc.flags = 0;
    desc.surface = surface;
    desc.minImageCount = image_count;
    desc.imageFormat = surface_format.format;
    desc.imageColorSpace = surface_format.colorSpace;
    desc.imageExtent = image_extent;
    desc.imageArrayLayers = 1;
    desc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    // The splash blits directly into a presentable image even with FBOs on.
    if (vk.swapchainTransferDst)
        desc.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!vk.fboActive)
        desc.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    desc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    desc.queueFamilyIndexCount = 0;
    desc.pQueueFamilyIndices = NULL;
    desc.preTransform = surface_caps.currentTransform;
    // Opaque is preferred, but is not required on every window-system surface.
    const VkCompositeAlphaFlagsKHR supportedAlpha = surface_caps.supportedCompositeAlpha;
    if (!supportedAlpha)
        ri.Error(ERR_FATAL, "Vulkan: surface advertises no composite-alpha mode");
    desc.compositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(supportedAlpha & (~supportedAlpha + 1));
    desc.presentMode = present_mode;
    desc.clipped = VK_TRUE;
    desc.oldSwapchain = VK_NULL_HANDLE;

    VK_REQUIRE_SUCCESS( qvkCreateSwapchainKHR( device, &desc, NULL, swapchain ) );

    uint32_t actualImageCount = 0;
    VK_REQUIRE_SUCCESS( qvkGetSwapchainImagesKHR( vk.device, vk.swapchain, &actualImageCount, NULL ) );
    // The driver can allocate more than minImageCount. Truncating this list
    // does not constrain acquire indices and can index uninitialized handles.
    if (!actualImageCount || actualImageCount > MAX_SWAPCHAIN_IMAGES)
        ri.Error(ERR_FATAL, "Vulkan: actual swapchain image count %u exceeds renderer capacity %u", actualImageCount, MAX_SWAPCHAIN_IMAGES);
    vk.swapchain_image_count = actualImageCount;
    VK_REQUIRE_SUCCESS( qvkGetSwapchainImagesKHR( vk.device, vk.swapchain, &vk.swapchain_image_count, vk.swapchain_images ) );

    for ( i = 0; i < vk.swapchain_image_count; i++ ) {

        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.pNext = NULL;
        view.flags = 0;
        view.image = vk.swapchain_images[i];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = vk.present_format.format;
        view.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.baseMipLevel = 0;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.baseArrayLayer = 0;
        view.subresourceRange.layerCount = 1;

        VK_REQUIRE_SUCCESS( qvkCreateImageView( vk.device, &view, NULL, &vk.swapchain_image_views[i] ) );

        VK_SET_OBJECT_NAME( vk.swapchain_images[i], va( "swapchain image %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );
        VK_SET_OBJECT_NAME( vk.swapchain_image_views[i], va( "swapchain image %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT );
    }

	for ( i = 0; i < vk.swapchain_image_count; i++ ) {
		VkSemaphoreCreateInfo s;
		s.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		s.pNext = NULL;
		s.flags = 0;
		VK_REQUIRE_SUCCESS( qvkCreateSemaphore( vk.device, &s, NULL, &vk.swapchain_rendering_finished[i] ) );
		VK_SET_OBJECT_NAME( vk.swapchain_rendering_finished[i], va( "swapchain_rendering_finished semaphore %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_SEMAPHORE_EXT );
	}
#if 0
    if ( vk.initSwapchainLayout != VK_IMAGE_LAYOUT_UNDEFINED ) {
        VkCommandBuffer command_buffer = vk_begin_command_buffer();

        for (i = 0; i < vk.swapchain_image_count; i++) {
            vk_record_image_layout_transition( command_buffer, vk.swapchain_images[i], VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED,
                vk.initSwapchainLayout, 0, 0 );
        }

        vk_end_command_buffer( command_buffer, __func__ );
    }
#endif
	for ( i = 0; i < vk.swapchain_image_count; i++ ) {
		if ( vk.initSwapchainLayout != VK_IMAGE_LAYOUT_UNDEFINED ) {
			// The Vulkan spec states : Use of a presentable image must occur only after the image is returned by vkAcquireNextImageKHR,
			// and before it is released by vkQueuePresentKHR.
			// This includes transitioning the image layout and rendering commands(https ://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainKHR.html#_description)
			vk.swapchain_images_inited[i] = qfalse;
		} else {
			vk.swapchain_images_inited[i] = qtrue; // assume undefined layout
		}
	}
}

void vk_destroy_swapchain ( void ) {
    uint32_t i;

    for ( i = 0; i < vk.swapchain_image_count; i++ ) {
        if ( vk.swapchain_image_views[i] != VK_NULL_HANDLE ) {
            qvkDestroyImageView( vk.device, vk.swapchain_image_views[i], NULL );
            vk.swapchain_image_views[i] = VK_NULL_HANDLE;
        }
		if ( vk.swapchain_rendering_finished[i] != VK_NULL_HANDLE ) {
			qvkDestroySemaphore( vk.device, vk.swapchain_rendering_finished[i], NULL );
			vk.swapchain_rendering_finished[i] = VK_NULL_HANDLE;
		}
    }

    qvkDestroySwapchainKHR( vk.device, vk.swapchain, NULL );
}
