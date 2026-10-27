/**
 * @file state_buffer.c
 * @brief Circular State Buffer for Rollback/Replay
 * 
 * Manages game state snapshots with efficient memory pooling.
 */

#include <ps4splitscreen/ps4splitscreen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// -----------------------------------------------------------------------------
// State Buffer
// -----------------------------------------------------------------------------
typedef struct {
    uint8_t* data;           // Contiguous memory pool
    size_t state_size;       // Size of each state
    uint32_t capacity;       // Number of states
    uint32_t head;           // Next write index
    uint32_t count;          // Number of valid states
    bool* valid;             // Validity bitmap
    uint32_t* frame_numbers; // Frame number for each slot
} state_buffer_t;

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

state_buffer_t* ps4splitscreen_state_buffer_create(size_t state_size, uint32_t capacity) {
    if (state_size == 0 || capacity == 0) return NULL;

    state_buffer_t* buf = calloc(1, sizeof(state_buffer_t));
    if (!buf) return NULL;

    buf->state_size = state_size;
    buf->capacity = capacity;
    
    buf->data = calloc(capacity, state_size);
    if (!buf->data) {
        free(buf);
        return NULL;
    }

    buf->valid = calloc(capacity, sizeof(bool));
    if (!buf->valid) {
        free(buf->data);
        free(buf);
        return NULL;
    }

    buf->frame_numbers = calloc(capacity, sizeof(uint32_t));
    if (!buf->frame_numbers) {
        free(buf->valid);
        free(buf->data);
        free(buf);
        return NULL;
    }

    return buf;
}

void ps4splitscreen_state_buffer_destroy(state_buffer_t* buf) {
    if (!buf) return;
    free(buf->frame_numbers);
    free(buf->valid);
    free(buf->data);
    free(buf);
}

bool ps4splitscreen_state_buffer_write(state_buffer_t* buf, uint32_t frame, const void* state) {
    if (!buf || !state) return false;
    
    buf->head = buf->head % buf->capacity;
    memcpy(&buf->data[buf->head * buf->state_size], state, buf->state_size);
    buf->valid[buf->head] = true;
    buf->frame_numbers[buf->head] = frame;
    
    buf->head = (buf->head + 1) % buf->capacity;
    if (buf->count < buf->capacity) buf->count++;
    
    return true;
}

const void* ps4splitscreen_state_buffer_read(const state_buffer_t* buf, uint32_t frame) {
    if (!buf) return NULL;
    
    // Search backwards from head (most recent first)
    for (uint32_t i = 0; i < buf->count; i++) {
        uint32_t idx = (buf->head + buf->capacity - 1 - i) % buf->capacity;
        if (buf->valid[idx] && buf->frame_numbers[idx] == frame) {
            return &buf->data[idx * buf->state_size];
        }
    }
    return NULL;
}

const void* ps4splitscreen_state_buffer_read_latest(const state_buffer_t* buf, uint32_t* out_frame) {
    if (!buf || buf->count == 0) return NULL;
    
    uint32_t idx = (buf->head + buf->capacity - 1) % buf->capacity;
    if (out_frame) *out_frame = buf->frame_numbers[idx];
    return &buf->data[idx * buf->state_size];
}

bool ps4splitscreen_state_buffer_has_frame(const state_buffer_t* buf, uint32_t frame) {
    if (!buf) return false;
    
    for (uint32_t i = 0; i < buf->count; i++) {
        uint32_t idx = (buf->head + buf->capacity - 1 - i) % buf->capacity;
        if (buf->valid[idx] && buf->frame_numbers[idx] == frame) {
            return true;
        }
    }
    return false;
}

void ps4splitscreen_state_buffer_clear(state_buffer_t* buf) {
    if (!buf) return;
    memset(buf->valid, 0, buf->capacity * sizeof(bool));
    buf->head = 0;
    buf->count = 0;
}

uint32_t ps4splitscreen_state_buffer_get_count(const state_buffer_t* buf) {
    return buf ? buf->count : 0;
}

uint32_t ps4splitscreen_state_buffer_get_capacity(const state_buffer_t* buf) {
    return buf ? buf->capacity : 0;
}

size_t ps4splitscreen_state_buffer_get_state_size(const state_buffer_t* buf) {
    return buf ? buf->state_size : 0;
}

// -----------------------------------------------------------------------------
// Iteration
// -----------------------------------------------------------------------------
typedef struct {
    const state_buffer_t* buffer;
    uint32_t index;
    uint32_t iterated;
} state_buffer_iter_t;

state_buffer_iter_t ps4splitscreen_state_buffer_iter_begin(const state_buffer_t* buf) {
    return (state_buffer_iter_t){ .buffer = buf, .index = (buf->head + buf->capacity - 1) % buf->capacity, .iterated = 0 };
}

bool ps4splitscreen_state_buffer_iter_next(state_buffer_iter_t* iter, uint32_t* out_frame, const void** out_state) {
    if (!iter || !iter->buffer || iter->iterated >= iter->buffer->count) return false;
    
    const state_buffer_t* buf = iter->buffer;
    if (!buf->valid[iter->index]) {
        // Skip invalid
        iter->index = (iter->index + buf->capacity - 1) % buf->capacity;
        iter->iterated++;
        return ps4splitscreen_state_buffer_iter_next(iter, out_frame, out_state);
    }
    
    if (out_frame) *out_frame = buf->frame_numbers[iter->index];
    if (out_state) *out_state = &buf->data[iter->index * buf->state_size];
    
    iter->index = (iter->index + buf->capacity - 1) % buf->capacity;
    iter->iterated++;
    return true;
}