/**
 * @file rollback_netcode.c
 * @brief GGPO-Style Rollback Netcode
 * 
 * Predicts local inputs, rewinds and re-simulates on remote input arrival.
 * Better for higher latency connections, smoother gameplay feel.
 */

#include <ps4splitscreen/ps4splitscreen.h>
#include "packet_serialization.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// -----------------------------------------------------------------------------
// Rollback Constants
// -----------------------------------------------------------------------------
#define ROLLBACK_MAX_FRAMES 16
#define ROLLBACK_MAX_PLAYERS 2
#define ROLLBACK_CHECKPOINT_INTERVAL 4

// -----------------------------------------------------------------------------
// Rollback State
// -----------------------------------------------------------------------------
typedef struct {
    // Input history for each player [frame % ROLLBACK_MAX_FRAMES]
    ps4splitscreen_input_t input_history[ROLLBACK_MAX_PLAYERS][ROLLBACK_MAX_FRAMES];
    bool input_confirmed[ROLLBACK_MAX_PLAYERS][ROLLBACK_MAX_FRAMES];
    bool input_predicted[ROLLBACK_MAX_PLAYERS][ROLLBACK_MAX_FRAMES];
    
    // Game state checkpoints [frame % ROLLBACK_MAX_FRAMES]
    void* state_checkpoints[ROLLBACK_MAX_FRAMES];
    bool checkpoint_valid[ROLLBACK_MAX_FRAMES];
    
    uint32_t current_frame;
    uint32_t confirmed_frame;      // Highest frame with all inputs confirmed
    uint32_t simulated_frame;      // Highest frame simulated (including predictions)
    uint32_t last_remote_frame;    // Highest frame we have remote input for
    
    uint32_t local_player_id;
    uint32_t remote_player_id;
    uint32_t max_rollback_frames;
    uint32_t checkpoint_interval;
    bool enable_prediction;
    
    size_t state_size;
    
    // Callbacks
    void (*simulate_frame)(void* user, uint32_t frame, const ps4splitscreen_input_t* inputs, int count);
    void (*save_state)(void* user, void* state_buffer);
    void (*load_state)(void* user, const void* state_buffer);
    void (*free_state)(void* state_buffer);
    void* user_data;
    
    // Stats
    uint32_t rollbacks_performed;
    uint32_t frames_predicted;
    uint32_t frames_rewound;
} rollback_context_t;

// -----------------------------------------------------------------------------
// Internal Helpers
// -----------------------------------------------------------------------------
static uint64_t get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static inline int frame_idx(uint32_t frame) {
    return frame % ROLLBACK_MAX_FRAMES;
}

static void predict_input(rollback_context_t* ctx, uint32_t frame, uint32_t player) {
    // Simple prediction: repeat last known input
    int idx = frame_idx(frame - 1);
    int target_idx = frame_idx(frame);
    
    if (ctx->input_confirmed[player][idx] || ctx->input_predicted[player][idx]) {
        ctx->input_history[player][target_idx] = ctx->input_history[player][idx];
        ctx->input_history[player][target_idx].frame_number = frame;
        ctx->input_predicted[player][target_idx] = true;
        ctx->frames_predicted++;
    } else {
        // No history - zero input
        memset(&ctx->input_history[player][target_idx], 0, sizeof(ps4splitscreen_input_t));
        ctx->input_history[player][target_idx].frame_number = frame;
        ctx->input_predicted[player][target_idx] = true;
    }
}

static void create_checkpoint(rollback_context_t* ctx, uint32_t frame) {
    int idx = frame_idx(frame);
    if (ctx->checkpoint_valid[idx] && ctx->free_state) {
        ctx->free_state(ctx->state_checkpoints[idx]);
    }
    
    void* state = malloc(ctx->state_size);
    if (state && ctx->save_state) {
        ctx->save_state(ctx->user_data, state);
        ctx->state_checkpoints[idx] = state;
        ctx->checkpoint_valid[idx] = true;
    }
}

static bool restore_checkpoint(rollback_context_t* ctx, uint32_t frame) {
    int idx = frame_idx(frame);
    if (!ctx->checkpoint_valid[idx] || !ctx->load_state) return false;
    
    ctx->load_state(ctx->user_data, ctx->state_checkpoints[idx]);
    return true;
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

rollback_context_t* ps4splitscreen_rollback_create(uint32_t local_player_id,
                                                    uint32_t remote_player_id,
                                                    size_t state_size,
                                                    uint32_t max_rollback_frames,
                                                    uint32_t checkpoint_interval,
                                                    bool enable_prediction) {
    if (max_rollback_frames > ROLLBACK_MAX_FRAMES) max_rollback_frames = ROLLBACK_MAX_FRAMES;
    if (checkpoint_interval == 0) checkpoint_interval = ROLLBACK_CHECKPOINT_INTERVAL;

    rollback_context_t* ctx = calloc(1, sizeof(rollback_context_t));
    if (!ctx) return NULL;

    ctx->local_player_id = local_player_id;
    ctx->remote_player_id = remote_player_id;
    ctx->state_size = state_size;
    ctx->max_rollback_frames = max_rollback_frames;
    ctx->checkpoint_interval = checkpoint_interval;
    ctx->enable_prediction = enable_prediction;
    ctx->current_frame = 0;
    ctx->confirmed_frame = 0;
    ctx->simulated_frame = 0;
    ctx->last_remote_frame = 0;

    return ctx;
}

void ps4splitscreen_rollback_destroy(rollback_context_t* ctx) {
    if (!ctx) return;
    
    // Free all checkpoints
    for (int i = 0; i < ROLLBACK_MAX_FRAMES; i++) {
        if (ctx->checkpoint_valid[i] && ctx->free_state && ctx->state_checkpoints[i]) {
            ctx->free_state(ctx->state_checkpoints[i]);
        }
    }
    free(ctx);
}

void ps4splitscreen_rollback_set_callbacks(rollback_context_t* ctx,
    void (*simulate)(void*, uint32_t, const ps4splitscreen_input_t*, int),
    void (*save)(void*, void*),
    void (*load)(void*, const void*),
    void (*free_fn)(void*),
    void* user_data) {
    if (ctx) {
        ctx->simulate_frame = simulate;
        ctx->save_state = save;
        ctx->load_state = load;
        ctx->free_state = free_fn;
        ctx->user_data = user_data;
    }
}

// Submit local input (confirmed immediately)
int ps4splitscreen_rollback_add_local_input(rollback_context_t* ctx, const ps4splitscreen_input_t* input) {
    if (!ctx || !input) return PS4SPLITSCREEN_ERR_INVALID_PARAM;
    if (input->player_id != ctx->local_player_id) return PS4SPLITSCREEN_ERR_INVALID_PARAM;

    int idx = frame_idx(input->frame_number);
    ctx->input_history[ctx->local_player_id][idx] = *input;
    ctx->input_confirmed[ctx->local_player_id][idx] = true;
    ctx->input_predicted[ctx->local_player_id][idx] = false;

    return PS4SPLITSCREEN_OK;
}

// Receive remote input (may trigger rollback)
int ps4splitscreen_rollback_add_remote_input(rollback_context_t* ctx, const ps4splitscreen_input_t* input) {
    if (!ctx || !input) return PS4SPLITSCREEN_ERR_INVALID_PARAM;
    if (input->player_id != ctx->remote_player_id) return PS4SPLITSCREEN_ERR_INVALID_PARAM;

    uint32_t frame = input->frame_number;
    
    // Ignore old frames
    if (frame <= ctx->last_remote_frame) return PS4SPLITSCREEN_OK;
    if (frame > ctx->current_frame + ctx->max_rollback_frames) return PS4SPLITSCREEN_ERR_INVALID_PARAM;

    int idx = frame_idx(frame);
    bool was_predicted = ctx->input_predicted[ctx->remote_player_id][idx];
    bool was_confirmed = ctx->input_confirmed[ctx->remote_player_id][idx];
    
    ctx->input_history[ctx->remote_player_id][idx] = *input;
    ctx->input_confirmed[ctx->remote_player_id][idx] = true;
    ctx->input_predicted[ctx->remote_player_id][idx] = false;
    ctx->last_remote_frame = frame;

    // Check if we need to rollback
    if (was_predicted && frame <= ctx->simulated_frame) {
        // We predicted differently - need to rollback
        return ps4splitscreen_rollback_perform(ctx, frame);
    }

    return PS4SPLITSCREEN_OK;
}

// Perform rollback to frame and re-simulate
int ps4splitscreen_rollback_perform(rollback_context_t* ctx, uint32_t rollback_to_frame) {
    if (!ctx) return PS4SPLITSCREEN_ERR_INVALID_PARAM;
    if (rollback_to_frame > ctx->simulated_frame) return PS4SPLITSCREEN_OK; // Nothing to rollback
    if (ctx->current_frame - rollback_to_frame > ctx->max_rollback_frames) {
        // Too far back - desync
        return PS4SPLITSCREEN_ERR_DESYNC_DETECTED;
    }

    // Find nearest checkpoint at or before rollback frame
    uint32_t checkpoint_frame = 0;
    for (uint32_t f = rollback_to_frame; f <= ctx->simulated_frame; f++) {
        if (ctx->checkpoint_valid[frame_idx(f)]) {
            checkpoint_frame = f;
            break;
        }
    }
    
    if (checkpoint_frame == 0 && rollback_to_frame > 0) {
        // No checkpoint - use frame 0
        checkpoint_frame = 0;
    }

    // Restore state
    if (!restore_checkpoint(ctx, checkpoint_frame)) {
        return PS4SPLITSCREEN_ERR_DESYNC_DETECTED;
    }

    // Re-simulate from checkpoint to current simulated frame
    ps4splitscreen_input_t inputs[ROLLBACK_MAX_PLAYERS];
    
    for (uint32_t f = checkpoint_frame; f <= ctx->simulated_frame; f++) {
        // Gather inputs for this frame
        for (int p = 0; p < ROLLBACK_MAX_PLAYERS; p++) {
            int idx = frame_idx(f);
            if (ctx->input_confirmed[p][idx] || ctx->input_predicted[p][idx]) {
                inputs[p] = ctx->input_history[p][idx];
            } else {
                // Shouldn't happen if prediction is enabled
                memset(&inputs[p], 0, sizeof(ps4splitscreen_input_t));
                inputs[p].frame_number = f;
                inputs[p].player_id = p;
            }
        }
        
        if (ctx->simulate_frame) {
            ctx->simulate_frame(ctx->user_data, f, inputs, ROLLBACK_MAX_PLAYERS);
        }
        
        // Create checkpoint at intervals
        if ((f % ctx->checkpoint_interval) == 0) {
            create_checkpoint(ctx, f);
        }
    }

    ctx->frames_rewound += ctx->simulated_frame - rollback_to_frame;
    ctx->rollbacks_performed++;
    ctx->simulated_frame = ctx->current_frame; // We've caught up
    
    return PS4SPLITSCREEN_OK;
}

// Main simulation step - call once per frame
int ps4splitscreen_rollback_advance(rollback_context_t* ctx) {
    if (!ctx) return PS4SPLITSCREEN_ERR_INVALID_PARAM;

    // Predict local input for next frame if enabled
    if (ctx->enable_prediction) {
        predict_input(ctx, ctx->current_frame + 1, ctx->local_player_id);
    }

    // Gather inputs for current frame
    ps4splitscreen_input_t inputs[ROLLBACK_MAX_PLAYERS];
    bool all_confirmed = true;
    
    for (int p = 0; p < ROLLBACK_MAX_PLAYERS; p++) {
        int idx = frame_idx(ctx->current_frame);
        if (ctx->input_confirmed[p][idx]) {
            inputs[p] = ctx->input_history[p][idx];
        } else if (ctx->input_predicted[p][idx]) {
            inputs[p] = ctx->input_history[p][idx];
            all_confirmed = false;
        } else {
            // No input yet - predict if local, wait if remote
            if (p == ctx->local_player_id && ctx->enable_prediction) {
                predict_input(ctx, ctx->current_frame, p);
                inputs[p] = ctx->input_history[p][idx];
                all_confirmed = false;
            } else {
                // Remote input not arrived yet
                return PS4SPLITSCREEN_ERR_TIMEOUT;
            }
        }
    }

    // Simulate frame
    if (ctx->simulate_frame) {
        ctx->simulate_frame(ctx->user_data, ctx->current_frame, inputs, ROLLBACK_MAX_PLAYERS);
    }

    // Create checkpoint
    if ((ctx->current_frame % ctx->checkpoint_interval) == 0) {
        create_checkpoint(ctx, ctx->current_frame);
    }

    // Advance confirmed frame if all inputs confirmed
    if (all_confirmed) {
        ctx->confirmed_frame = ctx->current_frame;
    }

    ctx->current_frame++;
    ctx->simulated_frame = ctx->current_frame - 1;

    return (int)ctx->current_frame;
}

uint32_t ps4splitscreen_rollback_get_frame(const rollback_context_t* ctx) {
    return ctx ? ctx->current_frame : 0;
}

uint32_t ps4splitscreen_rollback_get_confirmed_frame(const rollback_context_t* ctx) {
    return ctx ? ctx->confirmed_frame : 0;
}

uint32_t ps4splitscreen_rollback_get_rollback_count(const rollback_context_t* ctx) {
    return ctx ? ctx->rollbacks_performed : 0;
}

uint32_t ps4splitscreen_rollback_get_predicted_frames(const rollback_context_t* ctx) {
    return ctx ? ctx->frames_predicted : 0;
}