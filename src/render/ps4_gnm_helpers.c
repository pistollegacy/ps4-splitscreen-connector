/**
 * @file ps4_gnm_helpers.c
 * @brief GNM Rendering Helpers
 * 
 * Common rendering utilities for PS4 homebrew.
 */

#include <ps4splitscreen/ps4splitscreen.h>
#include <ps4/gnm.h>
#include <ps4/gnmx.h>
#include <ps4/shader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// -----------------------------------------------------------------------------
// Shader Management
// -----------------------------------------------------------------------------
typedef struct {
    SceGnmShader shader;
    SceGnmSizeAlign size_align;
    void* gpu_address;
    SceUid memory_handle;
} gnm_shader_t;

gnm_shader_t* ps4splitscreen_gnm_load_shader(const char* path, SceGnmShaderStage stage) {
    // Read shader binary from file
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    void* data = malloc(size);
    if (!data) { fclose(f); return NULL; }
    
    fread(data, 1, size, f);
    fclose(f);
    
    // Create shader
    gnm_shader_t* shader = malloc(sizeof(gnm_shader_t));
    if (!shader) { free(data); return NULL; }
    
    int ret = sceGnmShaderInitialize(&shader->shader, data, size, stage);
    if (ret != SCE_OK) {
        free(shader);
        free(data);
        return NULL;
    }
    
    // Get GPU address (for binding)
    shader->size_align = sceGnmShaderGetSizeAlign(&shader->shader);
    shader->gpu_address = data; // In practice, need to allocate GPU memory and copy
    
    return shader;
}

void ps4splitscreen_gnm_destroy_shader(gnm_shader_t* shader) {
    if (!shader) return;
    free(shader->gpu_address);
    free(shader);
}

// -----------------------------------------------------------------------------
// Pipeline State
// -----------------------------------------------------------------------------
typedef struct {
    SceGnmGraphicsPipeline pipeline;
    SceGnmSizeAlign size_align;
    void* gpu_address;
} gnm_pipeline_t;

gnm_pipeline_t* ps4splitscreen_gnm_create_pipeline(
    const SceGnmGraphicsPipelineInitInfo* init_info) {
    
    gnm_pipeline_t* pipe = malloc(sizeof(gnm_pipeline_t));
    if (!pipe) return NULL;
    
    int ret = sceGnmGraphicsPipelineInitialize(&pipe->pipeline, init_info);
    if (ret != SCE_OK) {
        free(pipe);
        return NULL;
    }
    
    pipe->size_align = sceGnmGraphicsPipelineGetSizeAlign(&pipe->pipeline);
    pipe->gpu_address = NULL; // Would need GPU memory allocation
    
    return pipe;
}

void ps4splitscreen_gnm_destroy_pipeline(gnm_pipeline_t* pipe) {
    if (!pipe) return;
    free(pipe);
}

void ps4splitscreen_gnm_bind_pipeline(SceGnmCommandBuffer* cmd, gnm_pipeline_t* pipe) {
    if (!cmd || !pipe) return;
    sceGnmSetGraphicsPipeline(cmd, &pipe->pipeline);
}

// -----------------------------------------------------------------------------
// Vertex Buffer Helpers
// -----------------------------------------------------------------------------
typedef struct {
    SceGnmBuffer buffer;
    SceGnmSizeAlign size_align;
    void* gpu_address;
    SceUid memory_handle;
    size_t vertex_size;
    uint32_t vertex_count;
} gnm_vertex_buffer_t;

gnm_vertex_buffer_t* ps4splitscreen_gnm_create_vertex_buffer(size_t vertex_size, 
                                                              uint32_t vertex_count,
                                                              SceGnmBufferUsage usage) {
    gnm_vertex_buffer_t* vb = calloc(1, sizeof(gnm_vertex_buffer_t));
    if (!vb) return NULL;
    
    vb->vertex_size = vertex_size;
    vb->vertex_count = vertex_count;
    
    SceGnmBufferInitInfo init = {0};
    init.size = vertex_size * vertex_count;
    init.alignment = 256;
    init.usage = usage;
    init.flags = SCE_GNM_BUFFER_FLAG_CPU_GPU_COHERENT;
    
    int ret = sceGnmBufferInit(&vb->buffer, &init);
    if (ret != SCE_OK) {
        free(vb);
        return NULL;
    }
    
    vb->size_align = sceGnmBufferGetSizeAlign(&vb->buffer);
    // vb->gpu_address = sceGnmBufferGetBaseAddress(&vb->buffer); // For mapping
    
    return vb;
}

void ps4splitscreen_gnm_destroy_vertex_buffer(gnm_vertex_buffer_t* vb) {
    if (!vb) return;
    // Buffer cleanup handled by GNM
    free(vb);
}

void ps4splitscreen_gnm_bind_vertex_buffer(SceGnmCommandBuffer* cmd, 
                                            gnm_vertex_buffer_t* vb,
                                            uint32_t stream_index,
                                            uint32_t offset) {
    if (!cmd || !vb) return;
    sceGnmSetVertexBuffers(cmd, stream_index, 1, &vb->buffer, &offset);
}

// -----------------------------------------------------------------------------
// Index Buffer
// -----------------------------------------------------------------------------
typedef struct {
    SceGnmBuffer buffer;
    SceGnmSizeAlign size_align;
    uint32_t index_count;
    SceGnmIndexType index_type;
} gnm_index_buffer_t;

gnm_index_buffer_t* ps4splitscreen_gnm_create_index_buffer(uint32_t index_count, 
                                                            SceGnmIndexType index_type,
                                                            SceGnmBufferUsage usage) {
    gnm_index_buffer_t* ib = calloc(1, sizeof(gnm_index_buffer_t));
    if (!ib) return NULL;
    
    ib->index_count = index_count;
    ib->index_type = index_type;
    
    size_t index_size = (index_type == SCE_GNM_INDEX_TYPE_16) ? 2 : 4;
    
    SceGnmBufferInitInfo init = {0};
    init.size = index_size * index_count;
    init.alignment = 256;
    init.usage = usage;
    init.flags = SCE_GNM_BUFFER_FLAG_CPU_GPU_COHERENT;
    
    int ret = sceGnmBufferInit(&ib->buffer, &init);
    if (ret != SCE_OK) {
        free(ib);
        return NULL;
    }
    
    ib->size_align = sceGnmBufferGetSizeAlign(&ib->buffer);
    return ib;
}

void ps4splitscreen_gnm_destroy_index_buffer(gnm_index_buffer_t* ib) {
    if (!ib) return;
    free(ib);
}

void ps4splitscreen_gnm_bind_index_buffer(SceGnmCommandBuffer* cmd, gnm_index_buffer_t* ib) {
    if (!cmd || !ib) return;
    sceGnmSetIndexBuffer(cmd, &ib->buffer, ib->index_type);
}

// -----------------------------------------------------------------------------
// Constant Buffer / Uniforms
// -----------------------------------------------------------------------------
typedef struct {
    SceGnmBuffer buffer;
    void* mapped_ptr;
    size_t size;
} gnm_constant_buffer_t;

gnm_constant_buffer_t* ps4splitscreen_gnm_create_constant_buffer(size_t size) {
    gnm_constant_buffer_t* cb = calloc(1, sizeof(gnm_constant_buffer_t));
    if (!cb) return NULL;
    
    cb->size = size;
    
    SceGnmBufferInitInfo init = {0};
    init.size = size;
    init.alignment = 256;
    init.usage = SCE_GNM_BUFFER_USAGE_CPU_TO_GPU;
    init.flags = SCE_GNM_BUFFER_FLAG_CPU_GPU_COHERENT;
    
    int ret = sceGnmBufferInit(&cb->buffer, &init);
    if (ret != SCE_OK) {
        free(cb);
        return NULL;
    }
    
    cb->mapped_ptr = sceGnmBufferGetBaseAddress(&cb->buffer);
    return cb;
}

void ps4splitscreen_gnm_destroy_constant_buffer(gnm_constant_buffer_t* cb) {
    if (!cb) return;
    free(cb);
}

void ps4splitscreen_gnm_update_constant_buffer(gnm_constant_buffer_t* cb, const void* data, size_t size) {
    if (!cb || !data || size > cb->size) return;
    memcpy(cb->mapped_ptr, data, size);
}

void ps4splitscreen_gnm_bind_constant_buffer(SceGnmCommandBuffer* cmd,
                                              gnm_constant_buffer_t* cb,
                                              SceGnmShaderStage stage,
                                              uint32_t slot) {
    if (!cmd || !cb) return;
    sceGnmSetConstantBuffers(cmd, stage, slot, 1, &cb->buffer);
}

// -----------------------------------------------------------------------------
// Texture / Sampler Helpers
// -----------------------------------------------------------------------------
typedef struct {
    SceGnmTexture texture;
    SceGnmSampler sampler;
    SceGnmSizeAlign size_align;
} gnm_texture_t;

gnm_texture_t* ps4splitscreen_gnm_create_texture_2d(uint32_t width, uint32_t height,
                                                     SceGnmFormat format,
                                                     uint32_t mip_levels,
                                                     bool render_target,
                                                     bool shader_resource) {
    gnm_texture_t* tex = calloc(1, sizeof(gnm_texture_t));
    if (!tex) return NULL;
    
    SceGnmTextureInitInfo init = {0};
    init.width = width;
    init.height = height;
    init.format = format;
    init.mipLevels = mip_levels;
    init.arraySize = 1;
    init.sampleCount = 1;
    init.tileMode = SCE_GNM_TILE_MODE_COLOR_LINEAR;
    init.useAsRenderTarget = render_target;
    init.useAsShaderResource = shader_resource;
    init.useAsDepthStencilTarget = false;
    
    int ret = sceGnmTextureInit(&tex->texture, &init);
    if (ret != SCE_OK) {
        free(tex);
        return NULL;
    }
    
    // Default sampler
    SceGnmSamplerInitInfo samp_init = {0};
    samp_init.filterMode = SCE_GNM_FILTER_MODE_BILINEAR;
    samp_init.addressU = SCE_GNM_ADDRESS_MODE_CLAMP;
    samp_init.addressV = SCE_GNM_ADDRESS_MODE_CLAMP;
    samp_init.addressW = SCE_GNM_ADDRESS_MODE_CLAMP;
    samp_init.mipFilterMode = SCE_GNM_MIP_FILTER_MODE_LINEAR;
    sceGnmSamplerInit(&tex->sampler, &samp_init);
    
    tex->size_align = sceGnmTextureGetSizeAlign(&tex->texture);
    return tex;
}

void ps4splitscreen_gnm_destroy_texture(gnm_texture_t* tex) {
    if (!tex) return;
    free(tex);
}

void ps4splitscreen_gnm_bind_texture(SceGnmCommandBuffer* cmd,
                                      gnm_texture_t* tex,
                                      SceGnmShaderStage stage,
                                      uint32_t slot) {
    if (!cmd || !tex) return;
    sceGnmSetTextures(cmd, stage, slot, 1, &tex->texture);
    sceGnmSetSamplers(cmd, stage, slot, 1, &tex->sampler);
}

// -----------------------------------------------------------------------------
// Draw Helpers
// -----------------------------------------------------------------------------

void ps4splitscreen_gnm_draw_indexed(SceGnmCommandBuffer* cmd, 
                                      SceGnmPrimitiveType prim_type,
                                      uint32_t index_count,
                                      uint32_t start_index,
                                      int32_t base_vertex) {
    if (!cmd) return;
    sceGnmDrawIndex(cmd, prim_type, index_count, start_index, base_vertex, 1);
}

void ps4splitscreen_gnm_draw(SceGnmCommandBuffer* cmd,
                              SceGnmPrimitiveType prim_type,
                              uint32_t vertex_count,
                              uint32_t start_vertex,
                              uint32_t instance_count) {
    if (!cmd) return;
    sceGnmDraw(cmd, prim_type, vertex_count, start_vertex, instance_count);
}

void ps4splitscreen_gnm_draw_instanced_indexed(SceGnmCommandBuffer* cmd,
                                                SceGnmPrimitiveType prim_type,
                                                uint32_t index_count,
                                                uint32_t instance_count,
                                                uint32_t start_index,
                                                int32_t base_vertex,
                                                uint32_t start_instance) {
    if (!cmd) return;
    sceGnmDrawIndexInstanced(cmd, prim_type, index_count, instance_count, 
                             start_index, base_vertex, start_instance);
}

// -----------------------------------------------------------------------------
// Command Buffer Management
// -----------------------------------------------------------------------------
typedef struct {
    SceGnmCommandBuffer* cmd_buffer;
    SceGnmCommandBufferInitInfo init_info;
    void* gpu_memory;
    SceUid memory_handle;
} gnm_command_buffer_t;

gnm_command_buffer_t* ps4splitscreen_gnm_create_command_buffer(size_t size) {
    gnm_command_buffer_t* cb = calloc(1, sizeof(gnm_command_buffer_t));
    if (!cb) return NULL;
    
    cb->init_info.size = size;
    cb->init_info.alignment = SCE_GNM_COMMAND_BUFFER_ALIGNMENT;
    cb->init_info.type = SCE_GNM_COMMAND_BUFFER_TYPE_DIRECT;
    
    // Allocate GPU memory for command buffer
    // Simplified - in practice use sceKernelAllocMemBlock with SCE_KERNEL_MEMBLOCK_TYPE_GPU
    
    cb->cmd_buffer = malloc(sizeof(SceGnmCommandBuffer));
    if (!cb->cmd_buffer) {
        free(cb);
        return NULL;
    }
    
    int ret = sceGnmCommandBufferInitialize(cb->cmd_buffer, &cb->init_info);
    if (ret != SCE_OK) {
        free(cb->cmd_buffer);
        free(cb);
        return NULL;
    }
    
    return cb;
}

void ps4splitscreen_gnm_destroy_command_buffer(gnm_command_buffer_t* cb) {
    if (!cb) return;
    free(cb->cmd_buffer);
    free(cb);
}

void ps4splitscreen_gnm_begin_command_buffer(gnm_command_buffer_t* cb) {
    if (!cb) return;
    sceGnmCommandBufferReset(cb->cmd_buffer, 0);
}

void ps4splitscreen_gnm_end_command_buffer(gnm_command_buffer_t* cb) {
    if (!cb) return;
    sceGnmCommandBufferClose(cb->cmd_buffer);
}

SceGnmCommandBuffer* ps4splitscreen_gnm_get_command_buffer(gnm_command_buffer_t* cb) {
    return cb ? cb->cmd_buffer : NULL;
}

// -----------------------------------------------------------------------------
// Common Render State Setup
// -----------------------------------------------------------------------------
void ps4splitscreen_gnm_set_default_render_state(SceGnmCommandBuffer* cmd) {
    if (!cmd) return;
    
    // Depth test
    sceGnmSetDepthControl(cmd, SCE_GNM_DEPTH_CONTROL_Z_TEST_ENABLE, 
                          SCE_GNM_DEPTH_FUNC_LESS, true, true);
    
    // Blend
    SceGnmBlendInfo blend = {0};
    blend.enable = true;
    blend.srcAlpha = SCE_GNM_BLEND_ONE;
    blend.dstAlpha = SCE_GNM_BLEND_ZERO;
    blend.srcColor = SCE_GNM_BLEND_SRC_ALPHA;
    blend.dstColor = SCE_GNM_BLEND_ONE_MINUS_SRC_ALPHA;
    blend.blendFunc = SCE_GNM_BLEND_FUNC_ADD;
    sceGnmSetBlendControl(cmd, 0, &blend);
    
    // Rasterizer
    sceGnmSetFrontFace(cmd, SCE_GNM_FRONT_FACE_CCW);
    sceGnmSetCullMode(cmd, SCE_GNM_CULL_MODE_BACK);
    sceGnmSetDepthBias(cmd, 0, 0, 0);
    
    // Primitive restart
    sceGnmSetPrimitiveRestartEnable(cmd, false);
}

// -----------------------------------------------------------------------------
// Debug Marker (for GPU capture tools)
// -----------------------------------------------------------------------------
void ps4splitscreen_gnm_push_marker(SceGnmCommandBuffer* cmd, const char* name) {
    if (!cmd || !name) return;
    sceGnmPushMarker(cmd, name);
}

void ps4splitscreen_gnm_pop_marker(SceGnmCommandBuffer* cmd) {
    if (!cmd) return;
    sceGnmPopMarker(cmd);
}