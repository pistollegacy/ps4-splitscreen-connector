/**
 * @file lockstep_sync.c
 * @brief Deterministic Lockstep Synchronization
 * 
 * Traditional lockstep: wait for all inputs before advancing frame.
 * Good for low-latency LAN, simple to implement.
 */

#include <ps4splitscreen/ps4splitscreen.h>
#include "packet_serialization.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// -----------------------------------------------------------------------------
// Lockstep State
// -----------------------------------------------------------------------------
#define LOCKSTEP_MAX_PLAYERS 2
#define LOCKSTEP_INPUT_BUFFER 64

typedef struct {
    ps4splitscreen_input_t inputs[LOCKSTEP_MAX_PLAYERS];
    bool received[LOCKSTEP_MAX_PLAYERS];
    uint64_t recv_time_us[LOCKSTEP_MAX_PLAYERS];
} lockstep_frame_inputs_t;

typedef struct {
    uint32_t current_frame;
    uint32_t confirmed_frame;
    uint32_t local_player_id;
    uint32_t remote_player_id;
    
    lockstep_frame_inputs_t input_buffer[LOCKSTEP_INPUT_BUFFER];
    
    uint32_t turn_timeout_ms;
    uint64_t frame_start_us;
    
    // Callbacks
    void (*on_advance_frame)(void* user, uint32_t frame, const ps4splitscreen_input_t* inputs, int count);
    void (*on_timeout)(void* user, uint32_t frame, uint32_t missing_player);
    void* user_data;
    
    // Stats
    uint32_t frames_advanced;
    uint32_t timeouts;
    float avg_frame_time_ms;
} lockstep_context_t;

// -----------------------------------------------------------------------------
// Internal
// -----------------------------------------------------------------------------
static uint64_t get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static lockstep_frame_inputs_t* get_frame_slot(lockstep_context_t* ctx, uint32_t frame) {
    return &ctx->input_buffer[frame % LOCKSTEP_INPUT_BUFFER];
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

lockstep_context_t* ps4splitscreen_lockstep_create(uint32_t local_player_id, 
                                                    uint32_t remote_player_id,
                                                    uint32_t turn_timeout_ms) {
    lockstep_context_t* ctx = calloc(1, sizeof(lockstep_context_t));
    if (!ctx) return NULL;

    ctx->current_frame = 0;
    ctx->confirmed_frame = 0;
    ctx->local_player_id = local_player_id;
    ctx->remote_player_id = remote_player_id;
    ctx->turn_timeout_ms = turn_timeout_ms;
    ctx->frame_start_us = get_time_us();

    // Initialize input buffer
    for (int i = 0; i < LOCKSTEP_INPUT_BUFFER; i++) {
        ctx->input_buffer[i].received[0] = false;
        ctx->input_buffer[i].received[1] = false;
    }

    return ctx;
}

void ps4splitscreen_lockstep_destroy(lockstep_context_t* ctx) {
    free(ctx);
}

void ps4splitscreen_lockstep_set_callbacks(lockstep_context_t* ctx,
                                            void (*on_advance)(void*, uint32_t, const ps4splitscreen_input_t*, int),
                                            void (*on_timeout)(void*, uint32_t, uint32_t),
                                            void* user_data) {
    if (ctx) {
        ctx->on_advance_frame = on_advance;
        ctx->on_timeout = on_timeout;
        ctx->user_data = user_data;
    }
}

// Submit local input for current frame
int ps4splitscreen_lockstep_submit_input(lockstep_context_t* ctx, const ps4splitscreen_input_t* input) {
    if (!ctx || !input) return PS4SPLITSCREEN_ERR_INVALID_PARAM;
    if (input->frame_number != ctx->current_frame) return PS4SPLITSCREEN_ERR_INVALID_PARAM;
    if (input->player_id != ctx->local_player_id) return PS4SPLITSCREEN_ERR_INVALID_PARAM;

    lockstep_frame_inputs_t* slot = get_frame_slot(ctx, ctx->current_frame);
    slot->inputs[ctx->local_player_id] = *input;
    slot->received[ctx->local_player_id] = true;
    slot->recv_time_us[ctx->local_player_id] = get_time_us();

    return PS4SPLITSCREEN_OK;
}

// Receive remote input
int ps4splitscreen_lockstep_receive_input(lockstep_context_t* ctx, const ps4splitscreen_input_t* input) {
    if (!ctx || !input) return PS4SPLITSCREEN_ERR_INVALID_PARAM;
    if (input->player_id != ctx->remote_player_id) return PS4SPLITSCREEN_ERR_INVALID_PARAM;
    if (input->frame_number < ctx->confirmed_frame) return PS4SPLITSCREEN_OK; // Old frame, ignore
    if (input->frame_number > ctx->current_frame + 2) return PS4SPLITSCREEN_ERR_INVALID_PARAM; // Too far ahead

    lockstep_frame_inputs_t* slot = get_frame_slot(ctx, input->frame_number);
    slot->inputs[ctx->remote_player_id] = *input;
    slot->received[ctx->remote_player_id] = true;
    slot->recv_time_us[ctx->remote_player_id] = get_time_us();

    return PS4SPLITSCREEN_OK;
}

// Check if we can advance frame
bool ps4splitscreen_lockstep_can_advance(lockstep_context_t* ctx) {
    if (!ctx) return false;
    
    lockstep_frame_inputs_t* slot = get_frame_slot(ctx, ctx->current_frame);
    return slot->received[0] && slot->received[1];
}

// Advance to next frame (call when both inputs received)
int ps4splitscreen_lockstep_advance(lockstep_context_t* ctx) {
    if (!ctx) return PS4SPLITSCREEN_ERR_INVALID_PARAM;
    if (!ps4splitscreen_lockstep_can_advance(ctx)) return PS4SPLITSCREEN_ERR_TIMEOUT;

    lockstep_frame_inputs_t* slot = get_frame_slot(ctx, ctx->current_frame);
    
    // Call advance callback
    if (ctx->on_advance_frame) {
        ctx->on_advance_frame(ctx->user_data, ctx->current_frame, slot->inputs, LOCKSTEP_MAX_PLAYERS);
    }

    ctx->confirmed_frame = ctx->current_frame;
    ctx->current_frame++;
    ctx->frames_advanced++;
    
    // Calculate frame time
    uint64_t now = get_time_us();
    float frame_time = (float)(now - ctx->frame_start_us) / 1000.0f;
    ctx->avg_frame_time_ms = ctx->avg_frame_time_ms * 0.9f + frame_time * 0.1f;
    ctx->frame_start_us = now;

    // Clear old slot
    slot->received[0] = false;
    slot->received[1] = false;

    return (int)ctx->confirmed_frame;
}

// Update - check for timeouts
int ps4splitscreen_lockstep_update(lockstep_context_t* ctx) {
    if (!ctx) return PS4SPLITSCREEN_ERR_INVALID_PARAM;

    uint64_t now = get_time_us();
    lockstep_frame_inputs_t* slot = get_frame_slot(ctx, ctx->current_frame);
    
    // Check if we're waiting too long for remote input
    if (slot->received[ctx->local_player_id] && !slot->received[ctx->remote_player_id]) {
        uint64_t elapsed = now - ctx->frame_start_us;
        if (elapsed >= (uint64_t)ctx->turn_timeout_ms * 1000) {
            // Timeout - use predicted input or last known input
            if (ctx->on_timeout) {
                ctx->on_timeout(ctx->user_data, ctx->current_frame, ctx->remote_player_id);
            }
            ctx->timeouts++;
            
            // Use last known remote input (or zero)
            if (ctx->current_frame > 0) {
                lockstep_frame_inputs_t* prev = get_frame_slot(ctx, ctx->current_frame - 1);
                slot->inputs[ctx->remote_player_id] = prev->inputs[ctx->remote_player_id];
            } else {
                memset(&slot->inputs[ctx->remote_player_id], 0, sizeof(ps4splitscreen_input_t));
            }
            slot->received[ctx->remote_player_id] = true;
            slot->inputs[ctx->remote_player_id].frame_number = ctx->current_frame;
        }
    }

    return PS4SPLITSCREEN_OK;
}

uint32_t ps4splitscreen_lockstep_get_frame(const lockstep_context_t* ctx) {
    return ctx ? ctx->current_frame : 0;
}

uint32_t ps4splitscreen_lockstep_get_confirmed_frame(const lockstep_context_t* ctx) {
    return ctx ? ctx->confirmed_frame : 0;
}

float ps4splitscreen_lockstep_get_avg_frame_time(const lockstep_context_t* ctx) {
    return ctx ? ctx->avg_frame_time_ms : 0.0f;
}

uint32_t ps4splitscreen_lockstep_get_timeouts(const lockstep_context_t* ctx) {
    return ctx ? ctx->timeouts : 0;
}