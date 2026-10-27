/**
 * @file input_sync.h
 * @brief Input Synchronization & Controller Handling
 */

#ifndef PS4SPLITSCREEN_INPUT_SYNC_H
#define PS4SPLITSCREEN_INPUT_SYNC_H

#include <ps4splitscreen/ps4splitscreen.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct input_sync input_sync_t;

input_sync_t* ps4splitscreen_input_create(void);
void ps4splitscreen_input_destroy(input_sync_t* sync);

int ps4splitscreen_input_update(input_sync_t* sync);
const ps4splitscreen_input_t* ps4splitscreen_input_get(input_sync_t* sync, uint32_t player_index);
bool ps4splitscreen_input_is_connected(input_sync_t* sync, uint32_t player_index);
void ps4splitscreen_input_set_frame(input_sync_t* sync, uint32_t frame, uint64_t timestamp_us);

// Input queries
bool ps4splitscreen_input_is_pressed(const ps4splitscreen_input_t* input, uint32_t button);
bool ps4splitscreen_input_is_down(const ps4splitscreen_input_t* input, uint32_t button);
bool ps4splitscreen_input_is_released(const ps4splitscreen_input_t* input, uint32_t button);

float ps4splitscreen_input_get_stick_x(const ps4splitscreen_input_t* input, bool left_stick);
float ps4splitscreen_input_get_stick_y(const ps4splitscreen_input_t* input, bool left_stick);
float ps4splitscreen_input_get_trigger(const ps4splitscreen_input_t* input, bool left_trigger);

void ps4splitscreen_input_apply_deadzone(ps4splitscreen_input_t* input, float deadzone);

// Vibration
int ps4splitscreen_input_set_vibration(input_sync_t* sync, uint32_t player_index, 
                                        uint8_t small_motor, uint8_t large_motor);

// Button constants
extern const uint32_t PS4SPLITSCREEN_BUTTON_CROSS;
extern const uint32_t PS4SPLITSCREEN_BUTTON_CIRCLE;
extern const uint32_t PS4SPLITSCREEN_BUTTON_TRIANGLE;
extern const uint32_t PS4SPLITSCREEN_BUTTON_SQUARE;
extern const uint32_t PS4SPLITSCREEN_BUTTON_L1;
extern const uint32_t PS4SPLITSCREEN_BUTTON_R1;
extern const uint32_t PS4SPLITSCREEN_BUTTON_L2;
extern const uint32_t PS4SPLITSCREEN_BUTTON_R2;
extern const uint32_t PS4SPLITSCREEN_BUTTON_L3;
extern const uint32_t PS4SPLITSCREEN_BUTTON_R3;
extern const uint32_t PS4SPLITSCREEN_BUTTON_OPTIONS;
extern const uint32_t PS4SPLITSCREEN_BUTTON_TOUCHPAD;
extern const uint32_t PS4SPLITSCREEN_BUTTON_PS;
extern const uint32_t PS4SPLITSCREEN_BUTTON_DPAD_UP;
extern const uint32_t PS4SPLITSCREEN_BUTTON_DPAD_DOWN;
extern const uint32_t PS4SPLITSCREEN_BUTTON_DPAD_LEFT;
extern const uint32_t PS4SPLITSCREEN_BUTTON_DPAD_RIGHT;

#ifdef __cplusplus
}
#endif

#endif // PS4SPLITSCREEN_INPUT_SYNC_H