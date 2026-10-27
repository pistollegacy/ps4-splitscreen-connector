/**
 * @file rollback_netcode.h
 * @brief GGPO-Style Rollback Netcode
 */

#ifndef PS4SPLITSCREEN_ROLLBACK_NETCODE_H
#define PS4SPLITSCREEN_ROLLBACK_NETCODE_H

#include <ps4splitscreen/ps4splitscreen.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rollback_context rollback_context_t;

rollback_context_t* ps4splitscreen_rollback_create(uint32_t local_player_id,
                                                    uint32_t remote_player_id,
                                                    size_t state_size,
                                                    uint32_t max_rollback_frames,
                                                    uint32_t checkpoint_interval,
                                                    bool enable_prediction);
void ps4splitscreen_rollback_destroy(rollback_context_t* ctx);

void ps4splitscreen_rollback_set_callbacks(rollback_context_t* ctx,
    void (*simulate)(void*, uint32_t, const ps4splitscreen_input_t*, int),
    void (*save)(void*, void*),
    void (*load)(void*, const void*),
    void (*free_fn)(void*),
    void* user_data);

int ps4splitscreen_rollback_add_local_input(rollback_context_t* ctx, const ps4splitscreen_input_t* input);
int ps4splitscreen_rollback_add_remote_input(rollback_context_t* ctx, const ps4splitscreen_input_t* input);
int ps4splitscreen_rollback_perform(rollback_context_t* ctx, uint32_t rollback_to_frame);
int ps4splitscreen_rollback_advance(rollback_context_t* ctx);

uint32_t ps4splitscreen_rollback_get_frame(const rollback_context_t* ctx);
uint32_t ps4splitscreen_rollback_get_confirmed_frame(const rollback_context_t* ctx);
uint32_t ps4splitscreen_rollback_get_rollback_count(const rollback_context_t* ctx);
uint32_t ps4splitscreen_rollback_get_predicted_frames(const rollback_context_t* ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4SPLITSCREEN_ROLLBACK_NETCODE_H