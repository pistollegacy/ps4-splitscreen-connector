/**
 * @file deterministic_rng.h
 * @brief Deterministic Random Number Generator (PCG32)
 */

#ifndef PS4SPLITSCREEN_DETERMINISTIC_RNG_H
#define PS4SPLITSCREEN_DETERMINISTIC_RNG_H

#include <ps4splitscreen/ps4splitscreen.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct deterministic_rng deterministic_rng_t;

deterministic_rng_t* ps4splitscreen_rng_create(uint64_t seed, uint64_t sequence, bool per_frame);
void ps4splitscreen_rng_destroy(deterministic_rng_t* rng);

void ps4splitscreen_rng_set_frame(deterministic_rng_t* rng, uint32_t frame);
void ps4splitscreen_rng_reseed(deterministic_rng_t* rng, uint64_t seed, uint64_t sequence);

uint32_t ps4splitscreen_rng_next_u32(deterministic_rng_t* rng);
uint64_t ps4splitscreen_rng_next_u64(deterministic_rng_t* rng);
float ps4splitscreen_rng_next_float(deterministic_rng_t* rng);
float ps4splitscreen_rng_next_float_range(deterministic_rng_t* rng, float min, float max);
int32_t ps4splitscreen_rng_next_int(deterministic_rng_t* rng, int32_t min, int32_t max);
bool ps4splitscreen_rng_next_bool(deterministic_rng_t* rng, float probability);
void ps4splitscreen_rng_next_bytes(deterministic_rng_t* rng, void* buffer, size_t size);
void ps4splitscreen_rng_shuffle(deterministic_rng_t* rng, void* array, size_t count, size_t elem_size);

uint32_t ps4splitscreen_rng_get_call_count(const deterministic_rng_t* rng);
void ps4splitscreen_rng_save_state(const deterministic_rng_t* rng, uint64_t* out_state, uint64_t* out_inc);
void ps4splitscreen_rng_load_state(deterministic_rng_t* rng, uint64_t state, uint64_t inc);

// Global RNG
void ps4splitscreen_rng_global_init(uint64_t seed, uint64_t sequence);
void ps4splitscreen_rng_global_term(void);
uint32_t ps4splitscreen_rng_global_u32(void);
float ps4splitscreen_rng_global_float(void);

#ifdef __cplusplus
}
#endif

#endif // PS4SPLITSCREEN_DETERMINISTIC_RNG_H