/**
 * @file input_sync.c
 * @brief Input Synchronization & Controller Handling
 * 
 * Reads PS4 controller input and prepares it for network synchronization.
 */

#include <ps4splitscreen/ps4splitscreen.h>
#include <ps4/pad.h>
#include <ps4/user_service.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// -----------------------------------------------------------------------------
// Button Mappings (SCE_PAD_*)
// -----------------------------------------------------------------------------
#define BUTTON_CROSS      (1u << 0)   // SCE_PAD_BUTTON_CROSS
#define BUTTON_CIRCLE     (1u << 1)   // SCE_PAD_BUTTON_CIRCLE
#define BUTTON_TRIANGLE   (1u << 2)   // SCE_PAD_BUTTON_TRIANGLE
#define BUTTON_SQUARE     (1u << 3)   // SCE_PAD_BUTTON_SQUARE
#define BUTTON_L1         (1u << 4)   // SCE_PAD_BUTTON_L1
#define BUTTON_R1         (1u << 5)   // SCE_PAD_BUTTON_R1
#define BUTTON_L2         (1u << 6)   // SCE_PAD_BUTTON_L2
#define BUTTON_R2         (1u << 7)   // SCE_PAD_BUTTON_R2
#define BUTTON_L3         (1u << 8)   // SCE_PAD_BUTTON_L3
#define BUTTON_R3         (1u << 9)   // SCE_PAD_BUTTON_R3
#define BUTTON_OPTIONS    (1u << 10)  // SCE_PAD_BUTTON_OPTIONS
#define BUTTON_TOUCHPAD   (1u << 11)  // SCE_PAD_BUTTON_TOUCHPAD
#define BUTTON_PS         (1u << 12)  // SCE_PAD_BUTTON_PS
#define BUTTON_DPAD_UP    (1u << 13)  // SCE_PAD_BUTTON_UP
#define BUTTON_DPAD_DOWN  (1u << 14)  // SCE_PAD_BUTTON_DOWN
#define BUTTON_DPAD_LEFT  (1u << 15)  // SCE_PAD_BUTTON_LEFT
#define BUTTON_DPAD_RIGHT (1u << 16)  // SCE_PAD_BUTTON_RIGHT

// -----------------------------------------------------------------------------
// Input State
// -----------------------------------------------------------------------------
typedef struct {
    ScePadHandle handles[2];
    ScePadData data[2];
    ps4splitscreen_input_t current_input[2];
    ps4splitscreen_input_t previous_input[2];
    bool connected[2];
    uint32_t user_ids[2];
} input_sync_t;

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

input_sync_t* ps4splitscreen_input_create(void) {
    input_sync_t* sync = calloc(1, sizeof(input_sync_t));
    if (!sync) return NULL;
    
    // Initialize pad library
    scePadInit();
    
    // Open pad handles for two controllers
    for (int i = 0; i < 2; i++) {
        sync->handles[i] = scePadOpen(i, SCE_PAD_TYPE_STANDARD, 0, NULL);
        sync->connected[i] = (sync->handles[i] >= 0);
        
        // Get user ID for this controller
        SceUserServiceLoginUserIdList user_list;
        int ret = sceUserServiceGetLoginUserIdList(&user_list);
        if (ret == SCE_OK && user_list.count > i) {
            sync->user_ids[i] = user_list.userId[i];
        }
    }
    
    return sync;
}

void ps4splitscreen_input_destroy(input_sync_t* sync) {
    if (!sync) return;
    
    for (int i = 0; i < 2; i++) {
        if (sync->handles[i] >= 0) {
            scePadClose(sync->handles[i]);
        }
    }
    
    scePadTerm();
    free(sync);
}

int ps4splitscreen_input_update(input_sync_t* sync) {
    if (!sync) return PS4SPLITSCREEN_ERR_INVALID_PARAM;
    
    for (int i = 0; i < 2; i++) {
        if (!sync->connected[i]) continue;
        
        // Store previous
        sync->previous_input[i] = sync->current_input[i];
        
        // Read current pad state
        int ret = scePadReadState(sync->handles[i], &sync->data[i]);
        if (ret < 0) continue;
        
        // Convert to our input format
        ps4splitscreen_input_t* in = &sync->current_input[i];
        const ScePadData* pad = &sync->data[i];
        
        in->buttons_down = pad->buttons;
        in->buttons_pressed = pad->buttons & ~sync->previous_input[i].buttons_down;
        in->buttons_released = sync->previous_input[i].buttons_down & ~pad->buttons;
        
        // Sticks (already -32768 to 32767)
        in->left_stick_x = pad->leftStickX;
        in->left_stick_y = pad->leftStickY;
        in->right_stick_x = pad->rightStickX;
        in->right_stick_y = pad->rightStickY;
        
        // Triggers (0-255)
        in->left_trigger = pad->leftTrigger;
        in->right_trigger = pad->rightTrigger;
        
        // Player ID
        in->player_id = i;
    }
    
    return PS4SPLITSCREEN_OK;
}

const ps4splitscreen_input_t* ps4splitscreen_input_get(input_sync_t* sync, uint32_t player_index) {
    if (!sync || player_index >= 2) return NULL;
    return &sync->current_input[player_index];
}

bool ps4splitscreen_input_is_connected(input_sync_t* sync, uint32_t player_index) {
    if (!sync || player_index >= 2) return false;
    return sync->connected[player_index];
}

void ps4splitscreen_input_set_frame(input_sync_t* sync, uint32_t frame, uint64_t timestamp_us) {
    if (!sync) return;
    for (int i = 0; i < 2; i++) {
        sync->current_input[i].frame_number = frame;
        sync->current_input[i].timestamp_us = timestamp_us;
    }
}

// -----------------------------------------------------------------------------
// Input Helpers
// -----------------------------------------------------------------------------

bool ps4splitscreen_input_is_pressed(const ps4splitscreen_input_t* input, uint32_t button) {
    return input && (input->buttons_pressed & button) != 0;
}

bool ps4splitscreen_input_is_down(const ps4splitscreen_input_t* input, uint32_t button) {
    return input && (input->buttons_down & button) != 0;
}

bool ps4splitscreen_input_is_released(const ps4splitscreen_input_t* input, uint32_t button) {
    return input && (input->buttons_released & button) != 0;
}

float ps4splitscreen_input_get_stick_x(const ps4splitscreen_input_t* input, bool left_stick) {
    if (!input) return 0.0f;
    int16_t raw = left_stick ? input->left_stick_x : input->right_stick_x;
    return (float)raw / 32767.0f;
}

float ps4splitscreen_input_get_stick_y(const ps4splitscreen_input_t* input, bool left_stick) {
    if (!input) return 0.0f;
    int16_t raw = left_stick ? input->left_stick_y : input->right_stick_y;
    return (float)raw / 32767.0f;
}

float ps4splitscreen_input_get_trigger(const ps4splitscreen_input_t* input, bool left_trigger) {
    if (!input) return 0.0f;
    uint16_t raw = left_trigger ? input->left_trigger : input->right_trigger;
    return (float)raw / 255.0f;
}

// Deadzone handling
void ps4splitscreen_input_apply_deadzone(ps4splitscreen_input_t* input, float deadzone) {
    if (!input) return;
    
    auto apply = [deadzone](int16_t* stick_x, int16_t* stick_y) {
        float x = (float)*stick_x / 32767.0f;
        float y = (float)*stick_y / 32767.0f;
        float mag = sqrtf(x*x + y*y);
        
        if (mag < deadzone) {
            *stick_x = 0;
            *stick_y = 0;
        } else if (mag < 1.0f) {
            // Scale to compensate for deadzone
            float scale = (mag - deadzone) / (mag * (1.0f - deadzone));
            *stick_x = (int16_t)(x * scale * 32767.0f);
            *stick_y = (int16_t)(y * scale * 32767.0f);
        }
    };
    
    apply(&input->left_stick_x, &input->left_stick_y);
    apply(&input->right_stick_x, &input->right_stick_y);
}

// Vibration
int ps4splitscreen_input_set_vibration(input_sync_t* sync, uint32_t player_index, 
                                        uint8_t small_motor, uint8_t large_motor) {
    if (!sync || player_index >= 2 || !sync->connected[player_index]) {
        return PS4SPLITSCREEN_ERR_INVALID_PARAM;
    }
    
    ScePadVibrationParam vib = {0};
    vib.smallMotor = small_motor;
    vib.largeMotor = large_motor;
    
    return scePadSetVibration(sync->handles[player_index], &vib);
}

// -----------------------------------------------------------------------------
// Button Constants for User Code
// -----------------------------------------------------------------------------
const uint32_t PS4SPLITSCREEN_BUTTON_CROSS      = BUTTON_CROSS;
const uint32_t PS4SPLITSCREEN_BUTTON_CIRCLE     = BUTTON_CIRCLE;
const uint32_t PS4SPLITSCREEN_BUTTON_TRIANGLE   = BUTTON_TRIANGLE;
const uint32_t PS4SPLITSCREEN_BUTTON_SQUARE     = BUTTON_SQUARE;
const uint32_t PS4SPLITSCREEN_BUTTON_L1         = BUTTON_L1;
const uint32_t PS4SPLITSCREEN_BUTTON_R1         = BUTTON_R1;
const uint32_t PS4SPLITSCREEN_BUTTON_L2         = BUTTON_L2;
const uint32_t PS4SPLITSCREEN_BUTTON_R2         = BUTTON_R2;
const uint32_t PS4SPLITSCREEN_BUTTON_L3         = BUTTON_L3;
const uint32_t PS4SPLITSCREEN_BUTTON_R3         = BUTTON_R3;
const uint32_t PS4SPLITSCREEN_BUTTON_OPTIONS    = BUTTON_OPTIONS;
const uint32_t PS4SPLITSCREEN_BUTTON_TOUCHPAD   = BUTTON_TOUCHPAD;
const uint32_t PS4SPLITSCREEN_BUTTON_PS         = BUTTON_PS;
const uint32_t PS4SPLITSCREEN_BUTTON_DPAD_UP    = BUTTON_DPAD_UP;
const uint32_t PS4SPLITSCREEN_BUTTON_DPAD_DOWN  = BUTTON_DPAD_DOWN;
const uint32_t PS4SPLITSCREEN_BUTTON_DPAD_LEFT  = BUTTON_DPAD_LEFT;
const uint32_t PS4SPLITSCREEN_BUTTON_DPAD_RIGHT = BUTTON_DPAD_RIGHT;