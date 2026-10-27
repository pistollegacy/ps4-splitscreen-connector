/**
 * @file state_buffer.h
 * @brief Circular State Buffer for Rollback/Replay
 */

#ifndef PS4SPLITSCREEN_STATE_BUFFER_H
#define PS4SPLITSCREEN_STATE_BUFFER_H

#include <ps4splitscreen/ps4splitscreen.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct state_buffer state_buffer_t;

state_buffer_t* ps4splitscreen_state_buffer_create(size_t state_size, uint32_t capacity);
void ps4splitscreen_state_buffer_destroy(state_buffer_t* buf);

bool ps4splitscreen_state_buffer_write(state_buffer_t* buf, uint32_t frame, const void* state);
const void* ps4splitscreen_state_buffer_read(const state_buffer_t* buf, uint32_t frame);
const void* ps4splitscreen_state_buffer_read_latest(const state_buffer_t* buf, uint32_t* out_frame);
bool ps4splitscreen_state_buffer_has_frame(const state_buffer_t* buf, uint32_t frame);
void ps4splitscreen_state_buffer_clear(state_buffer_t* buf);

uint32_t ps4splitscreen_state_buffer_get_count(const state_buffer_t* buf);
uint32_t ps4splitscreen_state_buffer_get_capacity(const state_buffer_t* buf);
size_t ps4splitscreen_state_buffer_get_state_size(const state_buffer_t* buf);

// Iteration
typedef struct {
    const state_buffer_t* buffer;
    uint32_t index;
    uint32_t iterated;
} state_buffer_iter_t;

state_buffer_iter_t ps4splitscreen_state_buffer_iter_begin(const state_buffer_t* buf);
bool ps4splitscreen_state_buffer_iter_next(state_buffer_iter_t* iter, uint32_t* out_frame, const void** out_state);

#ifdef __cplusplus
}
#endif

#endif // PS4SPLITSCREEN_STATE_BUFFER_H