/**
 * @file ps4_gnm_helpers.h
 * @brief GNM Rendering Helpers
 */

#ifndef PS4SPLITSCREEN_PS4_GNM_HELPERS_H
#define PS4SPLITSCREEN_PS4_GNM_HELPERS_H

#include <ps4splitscreen/ps4splitscreen.h>
#include <ps4/gnm.h>

#ifdef __cplusplus
extern "C" {
#endif

// Shader
typedef struct gnm_shader gnm_shader_t;
gnm_shader_t* ps4splitscreen_gnm_load_shader(const char* path, SceGnmShaderStage stage);
void ps4splitscreen_gnm_destroy_shader(gnm_shader_t* shader);

// Pipeline
typedef struct gnm_pipeline gnm_pipeline_t;
gnm_pipeline_t* ps4splitscreen_gnm_create_pipeline(const SceGnmGraphicsPipelineInitInfo* init_info);
void ps4splitscreen_gnm_destroy_pipeline(gnm_pipeline_t* pipe);
void ps4splitscreen_gnm_bind_pipeline(SceGnmCommandBuffer* cmd, gnm_pipeline_t* pipe);

// Vertex Buffer
typedef struct gnm_vertex_buffer gnm_vertex_buffer_t;
gnm_vertex_buffer_t* ps4splitscreen_gnm_create_vertex_buffer(size_t vertex_size, 
                                                              uint32_t vertex_count,
                                                              SceGnmBufferUsage usage);
void ps4splitscreen_gnm_destroy_vertex_buffer(gnm_vertex_buffer_t* vb);
void ps4splitscreen_gnm_bind_vertex_buffer(SceGnmCommandBuffer* cmd, 
                                            gnm_vertex_buffer_t* vb,
                                            uint32_t stream_index,
                                            uint32_t offset);

// Index Buffer
typedef struct gnm_index_buffer gnm_index_buffer_t;
gnm_index_buffer_t* ps4splitscreen_gnm_create_index_buffer(uint32_t index_count, 
                                                            SceGnmIndexType index_type,
                                                            SceGnmBufferUsage usage);
void ps4splitscreen_gnm_destroy_index_buffer(gnm_index_buffer_t* ib);
void ps4splitscreen_gnm_bind_index_buffer(SceGnmCommandBuffer* cmd, gnm_index_buffer_t* ib);

// Constant Buffer
typedef struct gnm_constant_buffer gnm_constant_buffer_t;
gnm_constant_buffer_t* ps4splitscreen_gnm_create_constant_buffer(size_t size);
void ps4splitscreen_gnm_destroy_constant_buffer(gnm_constant_buffer_t* cb);
void ps4splitscreen_gnm_update_constant_buffer(gnm_constant_buffer_t* cb, const void* data, size_t size);
void ps4splitscreen_gnm_bind_constant_buffer(SceGnmCommandBuffer* cmd,
                                              gnm_constant_buffer_t* cb,
                                              SceGnmShaderStage stage,
                                              uint32_t slot);

// Texture
typedef struct gnm_texture gnm_texture_t;
gnm_texture_t* ps4splitscreen_gnm_create_texture_2d(uint32_t width, uint32_t height,
                                                     SceGnmFormat format,
                                                     uint32_t mip_levels,
                                                     bool render_target,
                                                     bool shader_resource);
void ps4splitscreen_gnm_destroy_texture(gnm_texture_t* tex);
void ps4splitscreen_gnm_bind_texture(SceGnmCommandBuffer* cmd,
                                      gnm_texture_t* tex,
                                      SceGnmShaderStage stage,
                                      uint32_t slot);

// Draw
void ps4splitscreen_gnm_draw_indexed(SceGnmCommandBuffer* cmd, 
                                      SceGnmPrimitiveType prim_type,
                                      uint32_t index_count,
                                      uint32_t start_index,
                                      int32_t base_vertex);
void ps4splitscreen_gnm_draw(SceGnmCommandBuffer* cmd,
                              SceGnmPrimitiveType prim_type,
                              uint32_t vertex_count,
                              uint32_t start_vertex,
                              uint32_t instance_count);
void ps4splitscreen_gnm_draw_instanced_indexed(SceGnmCommandBuffer* cmd,
                                                SceGnmPrimitiveType prim_type,
                                                uint32_t index_count,
                                                uint32_t instance_count,
                                                uint32_t start_index,
                                                int32_t base_vertex,
                                                uint32_t start_instance);

// Command Buffer
typedef struct gnm_command_buffer gnm_command_buffer_t;
gnm_command_buffer_t* ps4splitscreen_gnm_create_command_buffer(size_t size);
void ps4splitscreen_gnm_destroy_command_buffer(gnm_command_buffer_t* cb);
void ps4splitscreen_gnm_begin_command_buffer(gnm_command_buffer_t* cb);
void ps4splitscreen_gnm_end_command_buffer(gnm_command_buffer_t* cb);
SceGnmCommandBuffer* ps4splitscreen_gnm_get_command_buffer(gnm_command_buffer_t* cb);

// Render State
void ps4splitscreen_gnm_set_default_render_state(SceGnmCommandBuffer* cmd);

// Debug Markers
void ps4splitscreen_gnm_push_marker(SceGnmCommandBuffer* cmd, const char* name);
void ps4splitscreen_gnm_pop_marker(SceGnmCommandBuffer* cmd);

#ifdef __cplusplus
}
#endif

#endif // PS4SPLITSCREEN_PS4_GNM_HELPERS_H