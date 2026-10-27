/**
 * @file lockstep_sync.h
 * @brief Deterministic Lockstep Synchronization
 */

#ifndef PS4SPLITSCREEN_LOCKSTEP_SYNC_H
#define PS4SPLITSCREEN_LOCKSTEP_SYNC_H

#include <ps4splitscreen/ps4splitscreen.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lockstep_context lockstep_context_t;

lockstep_context_t* ps4splitscreen_lockstep_create(uint32_t local_player_id, 
                                                    uint32_t remote_player_id,
                                                    uint32_t turn_timeout_ms);
void ps4splitscreen_lockstep_destroy(lockstep_context_t* ctx);

void ps4splitscreen_lockstep_set_callbacks(lockstep_context_t* ctx,
    void (*on_advance)(void*, uint32_t, const ps4splitscreen_input_t*, int),
    void (*on_timeout)(void*, uint32_t, uint32_t),
    void* user_data);

int ps4splitscreen_lockstep_submit_input(lockstep_context_t* ctx, const ps4splitscreen_input_t* input);
int ps4splitscreen_lockstep_receive_input(lockstep_context_t* ctx, const ps4splitscreen_input_t* input);

bool ps4splitscreen_lockstep_can_advance(lockstep_context_t* ctx);
int ps4splitscreen_lockstep_advance(lockstep_context_t* ctx);
int ps4splitscreen_lockstep_update(lockstep_context_t* ctx);

uint32_t ps4splitscreen_lockstep_get_frame(const lockstep_context_t* ctx);
uint32_t ps4splitscreen_lockstep_get_confirmed_frame(const lockstep_context_t* ctx);
float ps4splitscreen_lockstep_get_avg_frame_time(const lockstep_context_t* ctx);
uint32_t ps4splitscreen_lockstep_get_timeouts(const lockstep_context_t* ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4SPLITSCREEN_LOCKSTEP_SYNC_H