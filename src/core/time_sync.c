/**
 * @file time_sync.c
 * @brief Network Time Synchronization (NTP-style)
 * 
 * Synchronizes clocks between two PS4s for deterministic simulation.
 */

#include <ps4splitscreen/ps4splitscreen.h>
#include <ps4/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

// -----------------------------------------------------------------------------
// Time Sync State
// -----------------------------------------------------------------------------
#define TIME_SYNC_SAMPLES 8
#define TIME_SYNC_INTERVAL_MS 1000

typedef struct {
    // NTP-style offset calculation
    // T1 = client send time
    // T2 = server receive time
    // T3 = server send time
    // T4 = client receive time
    // offset = ((T2 - T1) + (T3 - T4)) / 2
    // delay = (T4 - T1) - (T3 - T2)
    
    uint64_t local_samples[TIME_SYNC_SAMPLES];
    uint64_t remote_samples[TIME_SYNC_SAMPLES];
    uint64_t t2_samples[TIME_SYNC_SAMPLES];
    uint64_t t3_samples[TIME_SYNC_SAMPLES];
    uint32_t sample_count;
    uint32_t sample_index;
    
    // Calculated values
    int64_t clock_offset_us;      // Remote time = local time + offset
    float clock_drift_ppm;        // Parts per million drift
    uint64_t last_sync_us;
    uint32_t sync_interval_ms;
    
    // Callbacks for packet send/receive
    void (*send_time_request)(void* user);
    void (*send_time_response)(void* user, uint64_t t1, uint64_t t2, uint64_t t3);
    void* user_data;
    
    bool initialized;
} time_sync_t;

// -----------------------------------------------------------------------------
// Internal
// -----------------------------------------------------------------------------
static uint64_t get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void calculate_offset(time_sync_t* sync) {
    if (sync->sample_count < 3) return; // Need at least 3 samples
    
    // Find sample with minimum round-trip delay
    int64_t best_offset = 0;
    uint64_t best_delay = UINT64_MAX;
    
    for (uint32_t i = 0; i < sync->sample_count; i++) {
        uint32_t idx = (sync->sample_index + i) % TIME_SYNC_SAMPLES;
        
        uint64_t t1 = sync->local_samples[idx];
        uint64_t t2 = sync->t2_samples[idx];
        uint64_t t3 = sync->t3_samples[idx];
        uint64_t t4 = sync->remote_samples[idx];
        
        // RTT = (T4 - T1) - (T3 - T2)
        int64_t rtt = (int64_t)(t4 - t1) - (int64_t)(t3 - t2);
        if (rtt < 0) rtt = 0;
        
        // Offset = ((T2 - T1) + (T3 - T4)) / 2
        int64_t offset = ((int64_t)t2 - (int64_t)t1 + (int64_t)t3 - (int64_t)t4) / 2;
        
        if ((uint64_t)rtt < best_delay) {
            best_delay = rtt;
            best_offset = offset;
        }
    }
    
    // Smooth the offset (exponential moving average)
    if (sync->initialized) {
        sync->clock_offset_us = (int64_t)(sync->clock_offset_us * 0.8 + best_offset * 0.2);
    } else {
        sync->clock_offset_us = best_offset;
        sync->initialized = true;
    }
    
    // Estimate drift from offset changes over time
    // (simplified - would need multiple sync rounds)
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

time_sync_t* ps4splitscreen_time_sync_create(uint32_t sync_interval_ms) {
    time_sync_t* sync = calloc(1, sizeof(time_sync_t));
    if (!sync) return NULL;
    
    sync->sync_interval_ms = sync_interval_ms ? sync_interval_ms : TIME_SYNC_INTERVAL_MS;
    sync->last_sync_us = 0;
    sync->initialized = false;
    
    return sync;
}

void ps4splitscreen_time_sync_destroy(time_sync_t* sync) {
    free(sync);
}

void ps4splitscreen_time_sync_set_callbacks(time_sync_t* sync,
    void (*send_request)(void*),
    void (*send_response)(void*, uint64_t, uint64_t, uint64_t),
    void* user_data) {
    if (sync) {
        sync->send_time_request = send_request;
        sync->send_time_response = send_response;
        sync->user_data = user_data;
    }
}

// Called by local client to initiate time sync
void ps4splitscreen_time_sync_request(time_sync_t* sync) {
    if (!sync || !sync->send_time_request) return;
    
    uint64_t t1 = get_time_us();
    sync->local_samples[sync->sample_index] = t1;
    sync->send_time_request(sync->user_data);
}

// Called by server when receiving time sync request
void ps4splitscreen_time_sync_on_request_received(time_sync_t* sync) {
    if (!sync || !sync->send_time_response) return;
    
    uint64_t t2 = get_time_us();
    // T1 is in the request packet, but we don't have it here
    // The response will include T1, T2, T3
}

// Called by client when receiving time sync response
void ps4splitscreen_time_sync_on_response_received(time_sync_t* sync, 
                                                    uint64_t t1, uint64_t t2, uint64_t t3) {
    if (!sync) return;
    
    uint64_t t4 = get_time_us();
    
    sync->remote_samples[sync->sample_index] = t4;
    sync->t2_samples[sync->sample_index] = t2;
    sync->t3_samples[sync->sample_index] = t3;
    // t1 already stored in local_samples
    
    sync->sample_count++;
    if (sync->sample_count > TIME_SYNC_SAMPLES) sync->sample_count = TIME_SYNC_SAMPLES;
    sync->sample_index = (sync->sample_index + 1) % TIME_SYNC_SAMPLES;
    
    calculate_offset(sync);
    sync->last_sync_us = t4;
}

// Get current remote time estimate
uint64_t ps4splitscreen_time_sync_get_remote_time(const time_sync_t* sync) {
    if (!sync) return 0;
    return get_time_us() + (uint64_t)sync->clock_offset_us;
}

// Get clock offset
int64_t ps4splitscreen_time_sync_get_offset(const time_sync_t* sync) {
    return sync ? sync->clock_offset_us : 0;
}

// Check if sync is needed
bool ps4splitscreen_time_sync_needed(const time_sync_t* sync) {
    if (!sync || !sync->initialized) return true;
    uint64_t now = get_time_us();
    return (now - sync->last_sync_us) >= (uint64_t)sync->sync_interval_ms * 1000;
}

// Get sync quality (0.0 = poor, 1.0 = excellent)
float ps4splitscreen_time_sync_get_quality(const time_sync_t* sync) {
    if (!sync || !sync->initialized) return 0.0f;
    
    // Quality based on sample count and delay variance
    float sample_quality = (float)sync->sample_count / TIME_SYNC_SAMPLES;
    
    // Calculate delay variance
    uint64_t delays[TIME_SYNC_SAMPLES];
    uint32_t count = 0;
    for (uint32_t i = 0; i < sync->sample_count; i++) {
        uint32_t idx = (sync->sample_index + i) % TIME_SYNC_SAMPLES;
        uint64_t t1 = sync->local_samples[idx];
        uint64_t t2 = sync->t2_samples[idx];
        uint64_t t3 = sync->t3_samples[idx];
        uint64_t t4 = sync->remote_samples[idx];
        
        int64_t rtt = (int64_t)(t4 - t1) - (int64_t)(t3 - t2);
        if (rtt >= 0) {
            delays[count++] = (uint64_t)rtt;
        }
    }
    
    if (count < 2) return sample_quality * 0.5f;
    
    // Mean delay
    uint64_t mean = 0;
    for (uint32_t i = 0; i < count; i++) mean += delays[i];
    mean /= count;
    
    // Variance
    float variance = 0;
    for (uint32_t i = 0; i < count; i++) {
        float diff = (float)delays[i] - (float)mean;
        variance += diff * diff;
    }
    variance /= count;
    
    // Quality decreases with variance
    float delay_quality = 1.0f / (1.0f + variance / 1000000.0f); // Normalize by 1ms^2
    
    return sample_quality * 0.5f + delay_quality * 0.5f;
}

// -----------------------------------------------------------------------------
// Frame Timing
// -----------------------------------------------------------------------------
typedef struct {
    uint64_t frame_start_us;
    uint64_t frame_end_us;
    float target_frame_time_ms;
    float accumulated_time_ms;
    uint32_t target_fps;
    uint32_t current_fps;
    uint32_t frame_count;
    uint64_t last_fps_update_us;
} frame_timer_t;

frame_timer_t* ps4splitscreen_frame_timer_create(uint32_t target_fps) {
    frame_timer_t* timer = calloc(1, sizeof(frame_timer_t));
    if (!timer) return NULL;
    
    timer->target_fps = target_fps ? target_fps : 60;
    timer->target_frame_time_ms = 1000.0f / timer->target_fps;
    timer->accumulated_time_ms = 0;
    timer->frame_start_us = get_time_us();
    timer->last_fps_update_us = timer->frame_start_us;
    
    return timer;
}

void ps4splitscreen_frame_timer_destroy(frame_timer_t* timer) {
    free(timer);
}

void ps4splitscreen_frame_timer_start_frame(frame_timer_t* timer) {
    if (!timer) return;
    timer->frame_start_us = get_time_us();
}

float ps4splitscreen_frame_timer_end_frame(frame_timer_t* timer) {
    if (!timer) return 0.0f;
    
    uint64_t now = get_time_us();
    timer->frame_end_us = now;
    
    float frame_time_ms = (float)(timer->frame_end_us - timer->frame_start_us) / 1000.0f;
    timer->accumulated_time_ms += frame_time_ms;
    timer->frame_count++;
    
    // Update FPS counter
    if ((now - timer->last_fps_update_us) >= 1000000) { // 1 second
        timer->current_fps = timer->frame_count;
        timer->frame_count = 0;
        timer->last_fps_update_us = now;
    }
    
    // Sleep if we're ahead
    if (frame_time_ms < timer->target_frame_time_ms) {
        float sleep_ms = timer->target_frame_time_ms - frame_time_ms;
        sceKernelUsleep((unsigned int)(sleep_ms * 1000));
        now = get_time_us();
        frame_time_ms = (float)(now - timer->frame_start_us) / 1000.0f;
    }
    
    return frame_time_ms;
}

float ps4splitscreen_frame_timer_get_delta(const frame_timer_t* timer) {
    return timer ? timer->target_frame_time_ms : 16.666f;
}

uint32_t ps4splitscreen_frame_timer_get_fps(const frame_timer_t* timer) {
    return timer ? timer->current_fps : 0;
}

bool ps4splitscreen_frame_timer_is_late(const frame_timer_t* timer) {
    if (!timer) return false;
    float frame_time = (float)(timer->frame_end_us - timer->frame_start_us) / 1000.0f;
    return frame_time > timer->target_frame_time_ms * 1.5f; // 50% over budget
}