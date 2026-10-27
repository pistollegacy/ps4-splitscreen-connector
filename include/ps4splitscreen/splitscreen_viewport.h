/**
 * @file splitscreen_viewport.h
 * @brief Splitscreen Viewport Management
 */

#ifndef PS4SPLITSCREEN_SPLITSCREEN_VIEWPORT_H
#define PS4SPLITSCREEN_SPLITSCREEN_VIEWPORT_H

#include <ps4splitscreen/ps4splitscreen.h>
#include <ps4/gnm.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct splitscreen_viewport splitscreen_viewport_t;

splitscreen_viewport_t* ps4splitscreen_viewport_create(const ps4splitscreen_viewport_config_t* config,
                                                        uint32_t width, uint32_t height);
void ps4splitscreen_viewport_destroy(splitscreen_viewport_t* ctx);

void ps4splitscreen_viewport_set_resolution(splitscreen_viewport_t* ctx, uint32_t width, uint32_t height);
void ps4splitscreen_viewport_set_layout(splitscreen_viewport_t* ctx, ps4splitscreen_layout_t layout);

bool ps4splitscreen_viewport_begin_player(splitscreen_viewport_t* ctx, uint32_t player_index,
                                           SceGnmCommandBuffer* cmd_buffer);
void ps4splitscreen_viewport_end_player(splitscreen_viewport_t* ctx, SceGnmCommandBuffer* cmd_buffer);

const SceGnmRect* ps4splitscreen_viewport_get_rect(const splitscreen_viewport_t* ctx, uint32_t player_index);
const SceGnmRect* ps4splitscreen_viewport_get_scissor(const splitscreen_viewport_t* ctx, uint32_t player_index);

void ps4splitscreen_viewport_render_debug(splitscreen_viewport_t* ctx, 
                                           SceGnmCommandBuffer* cmd,
                                           const ps4splitscreen_stats_t* stats,
                                           uint32_t local_player);
void ps4splitscreen_viewport_set_debug_visible(splitscreen_viewport_t* ctx, bool visible);

void ps4splitscreen_viewport_get_normalized_rect(ps4splitscreen_layout_t layout, 
                                                  uint32_t player, float out_rect[4]);

#ifdef __cplusplus
}
#endif

#endif // PS4SPLITSCREEN_SPLITSCREEN_VIEWPORT_H