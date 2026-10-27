/**
 * @file time_sync.h
 * @brief Network Time Synchronization
 */

#ifndef PS4SPLITSCREEN_TIME_SYNC_H
#define PS4SPLITSCREEN_TIME_SYNC_H

#include <ps4splitscreen/ps4splitscreen.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct time_sync time_sync_t;

time_sync_t* ps4splitscreen_time_sync_create(uint32_t sync_interval_ms);
void ps4splitscreen_time_sync_destroy(time_sync_t* sync);

void ps4splitscreen_time_sync_set_callbacks(time_sync_t* sync,
    void (*send_request)(void*),
    void (*send_response)(void*, uint64_t, uint64_t, uint64_t),
    void* user_data);

void ps4splitscreen_time_sync_request(time_sync_t* sync);
void ps4splitscreen_time_sync_on_request_received(time_sync_t* sync);
void ps4splitscreen_time_sync_on_response_received(time_sync_t* sync, 
                                                    uint64_t t1, uint64_t t2, uint64_t t3);

uint64_t ps4splitscreen_time_sync_get_remote_time(const time_sync_t* sync);
int64_t ps4splitscreen_time_sync_get_offset(const time_sync_t* sync);
bool ps4splitscreen_time_sync_needed(const time_sync_t* sync);
float ps4splitscreen_time_sync_get_quality(const time_sync_t* sync);

// Frame Timer
typedef struct frame_timer frame_timer_t;

frame_timer_t* ps4splitscreen_frame_timer_create(uint32_t target_fps);
void ps4splitscreen_frame_timer_destroy(frame_timer_t* timer);

void ps4splitscreen_frame_timer_start_frame(frame_timer_t* timer);
float ps4splitscreen_frame_timer_end_frame(frame_timer_t* timer);
float ps4splitscreen_frame_timer_get_delta(const frame_timer_t* timer);
uint32_t ps4splitscreen_frame_timer_get_fps(const frame_timer_t* timer);
bool ps4splitscreen_frame_timer_is_late(const frame_timer_t* timer);

#ifdef __cplusplus
}
#endif

#endif // PS4SPLITSCREEN_TIME_SYNC_H