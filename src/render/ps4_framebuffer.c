/**
 * @file ps4_framebuffer.c
 * @brief PS4 Framebuffer Management (GNM)
 * 
 * Handles render targets, depth buffers, and display swap chains.
 */

#include <ps4splitscreen/ps4splitscreen.h>
#include <ps4/gnm.h>
#include <ps4/gnmx.h>
#include <ps4/videoout.h>
#include <ps4/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// -----------------------------------------------------------------------------
// Framebuffer Structures
// -----------------------------------------------------------------------------
#define MAX_RENDER_TARGETS 3  // Triple buffering

typedef struct {
    SceGnmRenderTarget render_target;
    SceGnmTexture texture;
    SceGnmDepthRenderTarget depth_target;
    SceGnmTexture depth_texture;
    SceUid memory_handle;
    void* base_address;
} framebuffer_t;

typedef struct {
    framebuffer_t buffers[MAX_RENDER_TARGETS];
    uint32_t current_index;
    uint32_t width;
    uint32_t height;
    SceGnmColorFormat color_format;
    SceGnmDepthStencilFormat depth_format;
    uint32_t sample_count;  // 1, 2, 4, 8 for MSAA
    
    // Video output
    SceVideoOutHandle video_handle;
    SceVideoOutBufferAttribute buffer_attr;
    SceVideoOutFlipStatus flip_status;
} framebuffer_manager_t;

// -----------------------------------------------------------------------------
// Internal Helpers
// -----------------------------------------------------------------------------
static SceGnmTileMode choose_tile_mode(SceGnmColorFormat format, bool is_depth) {
    // Use displayable tile modes for scanout
    if (is_depth) {
        return SCE_GNM_TILE_MODE_DEPTH_STENCIL_DISPLAYABLE;
    }
    return SCE_GNM_TILE_MODE_COLOR_LINEAR_DISPLAYABLE;
}

static size_t align_size(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

framebuffer_manager_t* ps4splitscreen_framebuffer_create(uint32_t width, uint32_t height,
                                                          SceGnmColorFormat color_fmt,
                                                          SceGnmDepthStencilFormat depth_fmt,
                                                          uint32_t sample_count,
                                                          uint32_t buffer_count) {
    if (buffer_count > MAX_RENDER_TARGETS) buffer_count = MAX_RENDER_TARGETS;
    if (buffer_count < 2) buffer_count = 2; // At least double buffering
    
    framebuffer_manager_t* mgr = calloc(1, sizeof(framebuffer_manager_t));
    if (!mgr) return NULL;
    
    mgr->width = width;
    mgr->height = height;
    mgr->color_format = color_fmt;
    mgr->depth_format = depth_fmt;
    mgr->sample_count = sample_count;
    mgr->current_index = 0;
    
    // Initialize video output
    SceVideoOutHandle video_handle;
    int ret = sceVideoOutOpen(SCE_VIDEO_OUT_BUS_TYPE_MAIN, 0, &video_handle);
    if (ret < 0) {
        free(mgr);
        return NULL;
    }
    mgr->video_handle = video_handle;
    
    // Set up buffer attribute
    memset(&mgr->buffer_attr, 0, sizeof(mgr->buffer_attr));
    mgr->buffer_attr.format = SCE_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8; // Will be overridden
    mgr->buffer_attr.width = width;
    mgr->buffer_attr.height = height;
    mgr->buffer_attr.pitch = align_size(width * 4, 256); // 4 bytes per pixel for A8B8G8R8
    
    // Create framebuffers
    for (uint32_t i = 0; i < buffer_count; i++) {
        framebuffer_t* fb = &mgr->buffers[i];
        
        // Color texture
        SceGnmTextureInitInfo color_init = {0};
        color_init.width = width;
        color_init.height = height;
        color_init.format = color_fmt;
        color_init.mipLevels = 1;
        color_init.arraySize = 1;
        color_init.sampleCount = sample_count;
        color_init.tileMode = choose_tile_mode(color_fmt, false);
        color_init.useAsRenderTarget = true;
        color_init.useAsShaderResource = true;
        
        ret = sceGnmTextureInit(&fb->texture, &color_init);
        if (ret != SCE_OK) {
            // Cleanup on failure
            for (uint32_t j = 0; j < i; j++) {
                // TODO: cleanup
            }
            sceVideoOutClose(video_handle);
            free(mgr);
            return NULL;
        }
        
        // Depth texture
        SceGnmTextureInitInfo depth_init = {0};
        depth_init.width = width;
        depth_init.height = height;
        depth_init.format = depth_fmt;
        depth_init.mipLevels = 1;
        depth_init.arraySize = 1;
        depth_init.sampleCount = sample_count;
        depth_init.tileMode = choose_tile_mode(depth_fmt, true);
        depth_init.useAsDepthStencilTarget = true;
        
        ret = sceGnmTextureInit(&fb->depth_texture, &depth_init);
        if (ret != SCE_OK) {
            sceVideoOutClose(video_handle);
            free(mgr);
            return NULL;
        }
        
        // Render target from color texture
        SceGnmRenderTargetInitInfo rt_init = {0};
        rt_init.texture = &fb->texture;
        rt_init.mipLevel = 0;
        rt_init.slice = 0;
        
        ret = sceGnmRenderTargetInit(&fb->render_target, &rt_init);
        if (ret != SCE_OK) {
            sceVideoOutClose(video_handle);
            free(mgr);
            return NULL;
        }
        
        // Depth render target
        SceGnmDepthRenderTargetInitInfo drt_init = {0};
        drt_init.texture = &fb->depth_texture;
        drt_init.mipLevel = 0;
        drt_init.slice = 0;
        
        ret = sceGnmDepthRenderTargetInit(&fb->depth_target, &drt_init);
        if (ret != SCE_OK) {
            sceVideoOutClose(video_handle);
            free(mgr);
            return NULL;
        }
    }
    
    return mgr;
}

void ps4splitscreen_framebuffer_destroy(framebuffer_manager_t* mgr) {
    if (!mgr) return;
    
    for (uint32_t i = 0; i < MAX_RENDER_TARGETS; i++) {
        framebuffer_t* fb = &mgr->buffers[i];
        // Textures and render targets are cleaned up by GNM on process exit
    }
    
    sceVideoOutClose(mgr->video_handle);
    free(mgr);
}

// Get current render target
SceGnmRenderTarget* ps4splitscreen_framebuffer_get_current_rt(framebuffer_manager_t* mgr) {
    if (!mgr) return NULL;
    return &mgr->buffers[mgr->current_index].render_target;
}

SceGnmDepthRenderTarget* ps4splitscreen_framebuffer_get_current_depth(framebuffer_manager_t* mgr) {
    if (!mgr) return NULL;
    return &mgr->buffers[mgr->current_index].depth_target;
}

// Get shader resource view of current framebuffer
SceGnmTexture* ps4splitscreen_framebuffer_get_current_texture(framebuffer_manager_t* mgr) {
    if (!mgr) return NULL;
    return &mgr->buffers[mgr->current_index].texture;
}

// Advance to next buffer (call after presenting)
void ps4splitscreen_framebuffer_advance(framebuffer_manager_t* mgr) {
    if (!mgr) return;
    mgr->current_index = (mgr->current_index + 1) % MAX_RENDER_TARGETS;
}

// Present current framebuffer to display
int ps4splitscreen_framebuffer_present(framebuffer_manager_t* mgr, SceGnmCommandBuffer* cmd) {
    if (!mgr || !cmd) return -1;
    
    framebuffer_t* fb = &mgr->buffers[mgr->current_index];
    
    // Transition texture to displayable
    sceGnmSetTextures(cmd, SCE_GNM_SHADER_STAGE_PIXEL, 0, 1, &fb->texture);
    
    // Flip
    SceVideoOutFlipArg flip_arg = {0};
    flip_arg.buffer = &mgr->buffer_attr;
    flip_arg.flipMode = SCE_VIDEO_OUT_FLIP_MODE_VSYNC;
    flip_arg.callback = NULL;
    flip_arg.callbackArg = NULL;
    
    // Note: Actual flip requires registering buffers with sceVideoOutRegisterBuffers
    // This is simplified - full implementation needs buffer registration
    
    int ret = sceVideoOutFlip(mgr->video_handle, &flip_arg);
    if (ret < 0) return ret;
    
    // Wait for flip
    sceVideoOutWaitVblank(mgr->video_handle);
    
    return SCE_OK;
}

// Wait for vertical blank
void ps4splitscreen_framebuffer_wait_vblank(framebuffer_manager_t* mgr) {
    if (mgr) {
        sceVideoOutWaitVblank(mgr->video_handle);
    }
}

// Get video output handle
SceVideoOutHandle ps4splitscreen_framebuffer_get_video_handle(framebuffer_manager_t* mgr) {
    return mgr ? mgr->video_handle : 0;
}

// -----------------------------------------------------------------------------
// Command Buffer Helpers
// -----------------------------------------------------------------------------

void ps4splitscreen_framebuffer_setup_draw(framebuffer_manager_t* mgr, 
                                            SceGnmCommandBuffer* cmd,
                                            const float clear_color[4],
                                            float clear_depth,
                                            uint8_t clear_stencil) {
    if (!mgr || !cmd) return;
    
    framebuffer_t* fb = &mgr->buffers[mgr->current_index];
    
    // Set render targets
    sceGnmSetRenderTarget(cmd, 0, &fb->render_target);
    sceGnmSetDepthRenderTarget(cmd, &fb->depth_target);
    
    // Clear
    if (clear_color) {
        sceGnmClearRenderTarget(cmd, &fb->render_target, clear_color, 0, 0);
    }
    sceGnmClearDepthStencilTarget(cmd, &fb->depth_target, clear_depth, clear_stencil, 0, 0);
    
    // Set viewport
    SceGnmRect viewport = {0, 0, (int16_t)mgr->width, (int16_t)mgr->height};
    sceGnmSetViewport(cmd, &viewport);
    sceGnmSetScissorRect(cmd, &viewport);
    sceGnmSetScissorEnable(cmd, true);
}

void ps4splitscreen_framebuffer_end_draw(framebuffer_manager_t* mgr, SceGnmCommandBuffer* cmd) {
    if (!mgr || !cmd) return;
    
    // Disable scissor
    sceGnmSetScissorEnable(cmd, false);
    
    // Add barrier for texture read
    sceGnmResourceBarrier(cmd, SCE_GNM_RESOURCE_BARRIER_TYPE_TEXTURE,
                          SCE_GNM_GPU_PIPELINE_STAGE_RENDER_TARGET_OUTPUT,
                          SCE_GNM_GPU_PIPELINE_STAGE_PIXEL_SHADER,
                          1, &mgr->buffers[mgr->current_index].texture);
}