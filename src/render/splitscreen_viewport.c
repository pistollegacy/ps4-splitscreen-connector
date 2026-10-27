/**
 * @file splitscreen_viewport.c
 * @brief Splitscreen Viewport Management
 * 
 * Handles viewport configuration, scissor rects, and player view rendering.
 */

#include <ps4splitscreen/ps4splitscreen.h>
#include <ps4/gnm.h>
#include <ps4/gnmx.h>
#include <ps4/videoout.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// -----------------------------------------------------------------------------
// Viewport State
// -----------------------------------------------------------------------------
typedef struct {
    ps4splitscreen_viewport_config_t config;
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t current_player;  // 0 or 1, which player we're rendering for
    
    // Computed viewport rects in pixels
    SceGnmRect player_viewports[2];
    SceGnmRect player_scissors[2];
    
    // Debug overlay
    bool show_debug;
    uint32_t debug_font_texture;
} splitscreen_viewport_t;

// -----------------------------------------------------------------------------
// Internal Helpers
// -----------------------------------------------------------------------------
static void compute_viewports(splitscreen_viewport_t* ctx) {
    for (int p = 0; p < 2; p++) {
        float* vp = ctx->config.player0_viewport;
        if (p == 1) vp = ctx->config.player1_viewport;
        
        ctx->player_viewports[p].x = (int16_t)(vp[0] * ctx->screen_width);
        ctx->player_viewports[p].y = (int16_t)(vp[1] * ctx->screen_height);
        ctx->player_viewports[p].w = (int16_t)(vp[2] * ctx->screen_width);
        ctx->player_viewports[p].h = (int16_t)(vp[3] * ctx->screen_height);
        
        // Scissor matches viewport
        ctx->player_scissors[p] = ctx->player_viewports[p];
    }
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

splitscreen_viewport_t* ps4splitscreen_viewport_create(const ps4splitscreen_viewport_config_t* config,
                                                        uint32_t width, uint32_t height) {
    splitscreen_viewport_t* ctx = calloc(1, sizeof(splitscreen_viewport_t));
    if (!ctx) return NULL;
    
    if (config) {
        ctx->config = *config;
    } else {
        ctx->config = ps4splitscreen_viewport_config_default(PS4SPLITSCREEN_LAYOUT_HORIZONTAL);
    }
    
    ctx->screen_width = width;
    ctx->screen_height = height;
    ctx->current_player = 0;
    ctx->show_debug = ctx->config.render_debug_overlay;
    
    compute_viewports(ctx);
    
    return ctx;
}

void ps4splitscreen_viewport_destroy(splitscreen_viewport_t* ctx) {
    free(ctx);
}

void ps4splitscreen_viewport_set_resolution(splitscreen_viewport_t* ctx, uint32_t width, uint32_t height) {
    if (!ctx) return;
    ctx->screen_width = width;
    ctx->screen_height = height;
    compute_viewports(ctx);
}

void ps4splitscreen_viewport_set_layout(splitscreen_viewport_t* ctx, ps4splitscreen_layout_t layout) {
    if (!ctx) return;
    ctx->config.layout = layout;
    ctx->config = ps4splitscreen_viewport_config_default(layout);
    compute_viewports(ctx);
}

bool ps4splitscreen_viewport_begin_player(splitscreen_viewport_t* ctx, uint32_t player_index,
                                           SceGnmCommandBuffer* cmd_buffer) {
    if (!ctx || player_index >= 2 || !cmd_buffer) return false;
    
    ctx->current_player = player_index;
    
    // Set viewport
    sceGnmSetViewport(cmd_buffer, &ctx->player_viewports[player_index]);
    
    // Set scissor
    sceGnmSetScissorRect(cmd_buffer, &ctx->player_scissors[player_index]);
    sceGnmSetScissorEnable(cmd_buffer, true);
    
    return true;
}

void ps4splitscreen_viewport_end_player(splitscreen_viewport_t* ctx, SceGnmCommandBuffer* cmd_buffer) {
    if (!ctx || !cmd_buffer) return;
    
    // Disable scissor
    sceGnmSetScissorEnable(cmd_buffer, false);
    
    // Reset to full screen viewport
    SceGnmRect full = {0, 0, (int16_t)ctx->screen_width, (int16_t)ctx->screen_height};
    sceGnmSetViewport(cmd_buffer, &full);
}

// Get viewport rect for manual use
const SceGnmRect* ps4splitscreen_viewport_get_rect(const splitscreen_viewport_t* ctx, uint32_t player_index) {
    if (!ctx || player_index >= 2) return NULL;
    return &ctx->player_viewports[player_index];
}

const SceGnmRect* ps4splitscreen_viewport_get_scissor(const splitscreen_viewport_t* ctx, uint32_t player_index) {
    if (!ctx || player_index >= 2) return NULL;
    return &ctx->player_scissors[player_index];
}

// -----------------------------------------------------------------------------
// Debug Overlay Rendering
// -----------------------------------------------------------------------------

typedef struct {
    float x, y, z, w;
    float u, v;
    uint32_t color;
} debug_vertex_t;

static void draw_debug_text(splitscreen_viewport_t* ctx, SceGnmCommandBuffer* cmd,
                             float x, float y, const char* text, uint32_t color) {
    // Simple text rendering would go here
    // For now, just placeholder - would need font texture and shader
    (void)ctx; (void)cmd; (void)x; (void)y; (void)text; (void)color;
}

void ps4splitscreen_viewport_render_debug(splitscreen_viewport_t* ctx, 
                                           SceGnmCommandBuffer* cmd,
                                           const ps4splitscreen_stats_t* stats,
                                           uint32_t local_player) {
    if (!ctx || !ctx->show_debug || !cmd) return;
    
    // Render debug info in corner of each player's viewport
    const SceGnmRect* vp = &ctx->player_viewports[local_player];
    float x = vp->x + 10.0f;
    float y = vp->y + 10.0f;
    
    char buffer[256];
    
    // Frame info
    snprintf(buffer, sizeof(buffer), "Frame: %u / %u", stats->local_frame, stats->remote_frame);
    draw_debug_text(ctx, cmd, x, y, buffer, 0xFFFFFFFF); y += 20;
    
    // RTT
    snprintf(buffer, sizeof(buffer), "RTT: %.1fms (avg %.1fms)", stats->current_rtt_ms, stats->avg_rtt_ms);
    draw_debug_text(ctx, cmd, x, y, buffer, 0xFFFFFFFF); y += 20;
    
    // Jitter
    snprintf(buffer, sizeof(buffer), "Jitter: %.1fms", stats->jitter_ms);
    draw_debug_text(ctx, cmd, x, y, buffer, 0xFFFFFFFF); y += 20;
    
    // Frame advantage
    if (stats->frame_advantage > 0) {
        snprintf(buffer, sizeof(buffer), "Ahead: %d frames", stats->frame_advantage);
    } else if (stats->frame_advantage < 0) {
        snprintf(buffer, sizeof(buffer), "Behind: %d frames", -stats->frame_advantage);
    } else {
        snprintf(buffer, sizeof(buffer), "Synced");
    }
    draw_debug_text(ctx, cmd, x, y, buffer, stats->frame_advantage == 0 ? 0xFF00FF00 : 0xFFFFFF00); y += 20;
    
    // Rollbacks
    if (stats->rollbacks_count > 0) {
        snprintf(buffer, sizeof(buffer), "Rollbacks: %u", stats->rollbacks_count);
        draw_debug_text(ctx, cmd, x, y, buffer, 0xFFFF8800); y += 20;
    }
    
    // Packet loss
    if (stats->packets_lost > 0) {
        float loss_pct = stats->packets_sent > 0 ? 
            (float)stats->packets_lost / (float)stats->packets_sent * 100.0f : 0.0f;
        snprintf(buffer, sizeof(buffer), "Loss: %.1f%% (%llu/%llu)", loss_pct, 
                stats->packets_lost, stats->packets_sent);
        draw_debug_text(ctx, cmd, x, y, buffer, loss_pct > 5.0f ? 0xFFFF0000 : 0xFFFFFF00); y += 20;
    }
    
    // Bandwidth
    snprintf(buffer, sizeof(buffer), "BW: %.1f KB/s up / %.1f KB/s down",
             stats->bytes_sent / 1024.0f, stats->bytes_received / 1024.0f);
    draw_debug_text(ctx, cmd, x, y, buffer, 0xFFFFFFFF);
}

void ps4splitscreen_viewport_set_debug_visible(splitscreen_viewport_t* ctx, bool visible) {
    if (ctx) ctx->show_debug = visible;
}

// -----------------------------------------------------------------------------
// Layout Helpers
// -----------------------------------------------------------------------------

void ps4splitscreen_viewport_get_normalized_rect(ps4splitscreen_layout_t layout, 
                                                  uint32_t player, float out_rect[4]) {
    if (layout == PS4SPLITSCREEN_LAYOUT_HORIZONTAL) {
        if (player == 0) {
            out_rect[0] = 0.0f; out_rect[1] = 0.0f; out_rect[2] = 0.5f; out_rect[3] = 1.0f;
        } else {
            out_rect[0] = 0.5f; out_rect[1] = 0.0f; out_rect[2] = 0.5f; out_rect[3] = 1.0f;
        }
    } else {
        if (player == 0) {
            out_rect[0] = 0.0f; out_rect[1] = 0.0f; out_rect[2] = 1.0f; out_rect[3] = 0.5f;
        } else {
            out_rect[0] = 0.0f; out_rect[1] = 0.5f; out_rect[2] = 1.0f; out_rect[3] = 0.5f;
        }
    }
}