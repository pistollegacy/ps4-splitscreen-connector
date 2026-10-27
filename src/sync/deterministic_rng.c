/**
 * @file deterministic_rng.c
 * @brief Deterministic Random Number Generator
 * 
 * PCG (Permuted Congruential Generator) for synchronized RNG across clients.
 * Same seed + same sequence of calls = same results on all machines.
 */

#include <ps4splitscreen/ps4splitscreen.h>
#include <stdint.h>
#include <stdlib.h>

// -----------------------------------------------------------------------------
// PCG32 Implementation
// -----------------------------------------------------------------------------
typedef struct {
    uint64_t state;
    uint64_t inc;
} pcg32_t;

static uint32_t pcg32_random(pcg32_t* rng) {
    uint64_t oldstate = rng->state;
    rng->state = oldstate * 6364136223846793005ULL + (rng->inc | 1);
    uint32_t xorshifted = (uint32_t)(((oldstate >> 18u) ^ oldstate) >> 27u);
    uint32_t rot = (uint32_t)(oldstate >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

static void pcg32_srandom(pcg32_t* rng, uint64_t initstate, uint64_t initseq) {
    rng->state = 0;
    rng->inc = (initseq << 1) | 1;
    pcg32_random(rng);
    rng->state += initstate;
    pcg32_random(rng);
}

// -----------------------------------------------------------------------------
// Deterministic RNG Context
// -----------------------------------------------------------------------------
typedef struct {
    pcg32_t rng;
    uint32_t call_count;
    uint32_t frame_number;
    bool per_frame;  // If true, resets sequence each frame
} deterministic_rng_t;

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

deterministic_rng_t* ps4splitscreen_rng_create(uint64_t seed, uint64_t sequence, bool per_frame) {
    deterministic_rng_t* rng = malloc(sizeof(deterministic_rng_t));
    if (!rng) return NULL;
    
    pcg32_srandom(&rng->rng, seed, sequence);
    rng->call_count = 0;
    rng->frame_number = 0;
    rng->per_frame = per_frame;
    
    return rng;
}

void ps4splitscreen_rng_destroy(deterministic_rng_t* rng) {
    free(rng);
}

void ps4splitscreen_rng_set_frame(deterministic_rng_t* rng, uint32_t frame) {
    if (!rng) return;
    
    if (rng->per_frame && frame != rng->frame_number) {
        // Re-seed for new frame using frame number as sequence
        pcg32_srandom(&rng->rng, rng->rng.state, frame);
        rng->call_count = 0;
        rng->frame_number = frame;
    }
}

void ps4splitscreen_rng_reseed(deterministic_rng_t* rng, uint64_t seed, uint64_t sequence) {
    if (!rng) return;
    pcg32_srandom(&rng->rng, seed, sequence);
    rng->call_count = 0;
}

uint32_t ps4splitscreen_rng_next_u32(deterministic_rng_t* rng) {
    if (!rng) return 0;
    rng->call_count++;
    return pcg32_random(&rng->rng);
}

uint64_t ps4splitscreen_rng_next_u64(deterministic_rng_t* rng) {
    if (!rng) return 0;
    rng->call_count += 2;
    return ((uint64_t)pcg32_random(&rng->rng) << 32) | pcg32_random(&rng->rng);
}

float ps4splitscreen_rng_next_float(deterministic_rng_t* rng) {
    if (!rng) return 0.0f;
    rng->call_count++;
    return (float)pcg32_random(&rng->rng) / 4294967296.0f; // 2^32
}

float ps4splitscreen_rng_next_float_range(deterministic_rng_t* rng, float min, float max) {
    return min + ps4splitscreen_rng_next_float(rng) * (max - min);
}

int32_t ps4splitscreen_rng_next_int(deterministic_rng_t* rng, int32_t min, int32_t max) {
    if (min >= max) return min;
    uint32_t range = (uint32_t)(max - min + 1);
    return min + (int32_t)(pcg32_random(&rng->rng) % range);
}

bool ps4splitscreen_rng_next_bool(deterministic_rng_t* rng, float probability) {
    return ps4splitscreen_rng_next_float(rng) < probability;
}

void ps4splitscreen_rng_next_bytes(deterministic_rng_t* rng, void* buffer, size_t size) {
    if (!rng || !buffer) return;
    uint8_t* bytes = (uint8_t*)buffer;
    for (size_t i = 0; i < size; i += 4) {
        uint32_t val = pcg32_random(&rng->rng);
        size_t remaining = size - i;
        if (remaining >= 4) {
            *((uint32_t*)&bytes[i]) = val;
        } else {
            memcpy(&bytes[i], &val, remaining);
        }
    }
    rng->call_count += (size + 3) / 4;
}

// Shuffle array (Fisher-Yates)
void ps4splitscreen_rng_shuffle(deterministic_rng_t* rng, void* array, size_t count, size_t elem_size) {
    if (!rng || !array || count < 2) return;
    uint8_t* arr = (uint8_t*)array;
    uint8_t* tmp = malloc(elem_size);
    if (!tmp) return;
    
    for (size_t i = count - 1; i > 0; i--) {
        size_t j = pcg32_random(&rng->rng) % (i + 1);
        if (i != j) {
            memcpy(tmp, &arr[i * elem_size], elem_size);
            memcpy(&arr[i * elem_size], &arr[j * elem_size], elem_size);
            memcpy(&arr[j * elem_size], tmp, elem_size);
        }
    }
    free(tmp);
    rng->call_count += count;
}

uint32_t ps4splitscreen_rng_get_call_count(const deterministic_rng_t* rng) {
    return rng ? rng->call_count : 0;
}

void ps4splitscreen_rng_save_state(const deterministic_rng_t* rng, uint64_t* out_state, uint64_t* out_inc) {
    if (!rng) return;
    if (out_state) *out_state = rng->rng.state;
    if (out_inc) *out_inc = rng->rng.inc;
}

void ps4splitscreen_rng_load_state(deterministic_rng_t* rng, uint64_t state, uint64_t inc) {
    if (!rng) return;
    rng->rng.state = state;
    rng->rng.inc = inc;
}

// -----------------------------------------------------------------------------
// Global RNG (for convenience)
// -----------------------------------------------------------------------------
static deterministic_rng_t* g_global_rng = NULL;

void ps4splitscreen_rng_global_init(uint64_t seed, uint64_t sequence) {
    if (g_global_rng) ps4splitscreen_rng_destroy(g_global_rng);
    g_global_rng = ps4splitscreen_rng_create(seed, sequence, false);
}

void ps4splitscreen_rng_global_term(void) {
    if (g_global_rng) {
        ps4splitscreen_rng_destroy(g_global_rng);
        g_global_rng = NULL;
    }
}

uint32_t ps4splitscreen_rng_global_u32(void) {
    return g_global_rng ? ps4splitscreen_rng_next_u32(g_global_rng) : 0;
}

float ps4splitscreen_rng_global_float(void) {
    return g_global_rng ? ps4splitscreen_rng_next_float(g_global_rng) : 0.0f;
}