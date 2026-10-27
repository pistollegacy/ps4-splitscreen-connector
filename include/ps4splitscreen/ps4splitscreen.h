/**
 * @file ps4splitscreen.h
 * @brief Main public header for PS4SplitScreenConnector
 * 
 * LAN Splitscreen Framework for PS4 Homebrew (OpenOrbis/PS4SDK)
 * Enables two PS4s to synchronize game state over LAN for splitscreen co-op.
 * 
 * @author PS4 Homebrew Community
 * @version 1.0.0
 */

#ifndef PS4SPLITSCREEN_H
#define PS4SPlITSCREEN_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Version Information
// =============================================================================
#define PS4SPLITSCREEN_VERSION_MAJOR 1
#define PS4SPLITSCREEN_VERSION_MINOR 0
#define PS4SPLITSCREEN_VERSION_PATCH 0
#define PS4SPLITSCREEN_VERSION_STRING "1.0.0"

// =============================================================================
// Error Codes
// =============================================================================
typedef enum {
    PS4SPLITSCREEN_OK                    =  0,
    PS4SPLITSCREEN_ERR_INIT_FAILED       = -1,
    PS4SPLITSCREEN_ERR_NET_INIT_FAILED   = -2,
    PS4SPLITSCREEN_ERR_SOCKET_FAILED     = -3,
    PS4SPLITSCREEN_ERR_BIND_FAILED       = -4,
    PS4SPLITSCREEN_ERR_CONNECT_FAILED    = -5,
    PS4SPLITSCREEN_ERR_SEND_FAILED       = -6,
    PS4SPLITSCREEN_ERR_RECV_FAILED       = -7,
    PS4SPLITSCREEN_ERR_TIMEOUT           = -8,
    PS4SPLITSCREEN_ERR_INVALID_PARAM     = -9,
    PS4SPLITSCREEN_ERR_OUT_OF_MEMORY     = -10,
    PS4SPLITSCREEN_ERR_NOT_CONNECTED     = -11,
    PS4SPLITSCREEN_ERR_ALREADY_CONNECTED = -12,
    PS4SPLITSCREEN_ERR_PROTOCOL_MISMATCH = -13,
    PS4SPLITSCREEN_ERR_DESYNC_DETECTED   = -14,
    PS4SPLITSCREEN_ERR_GNM_FAILED        = -15,
    PS4SPLITSCREEN_ERR_VIDEO_OUT_FAILED  = -16,
} ps4splitscreen_error_t;

// =============================================================================
// Network Configuration
// =============================================================================
#define PS4SPLITSCREEN_DEFAULT_PORT       9337
#define PS4SPLITSCREEN_DISCOVERY_PORT     9338
#define PS4SPLITSCREEN_MAX_PLAYERS        2
#define PS4SPLITSCREEN_MAX_PACKET_SIZE    1472  // MTU-safe for LAN
#define PS4SPLITSCREEN_MAX_PAYLOAD_SIZE   1400
#define PS4SPLITSCREEN_PROTOCOL_VERSION   1

typedef struct {
    uint16_t game_port;           // Main game traffic port (UDP)
    uint16_t discovery_port;      // LAN discovery port (UDP broadcast)
    uint32_t connect_timeout_ms;  // Connection timeout
    uint32_t keepalive_interval_ms; // Keepalive interval
    uint32_t max_packet_loss;     // Max packets before disconnect
    bool enable_nagle;            // TCP-style Nagle for reliable channel
    uint32_t send_buffer_size;    // Socket send buffer
    uint32_t recv_buffer_size;    // Socket recv buffer
} ps4splitscreen_net_config_t;

static inline ps4splitscreen_net_config_t ps4splitscreen_net_config_default(void) {
    return (ps4splitscreen_net_config_t){
        .game_port = PS4SPLITSCREEN_DEFAULT_PORT,
        .discovery_port = PS4SPLITSCREEN_DISCOVERY_PORT,
        .connect_timeout_ms = 5000,
        .keepalive_interval_ms = 1000,
        .max_packet_loss = 100,
        .enable_nagle = false,
        .send_buffer_size = 256 * 1024,
        .recv_buffer_size = 256 * 1024,
    };
}

// =============================================================================
// Session Types
// =============================================================================
typedef enum {
    PS4SPLITSCREEN_SESSION_HOST = 0,  // This PS4 hosts the session
    PS4SPLITSCREEN_SESSION_CLIENT = 1, // This PS4 joins a session
} ps4splitscreen_session_role_t;

typedef enum {
    PS4SPLITSCREEN_STATE_DISCONNECTED = 0,
    PS4SPLITSCREEN_STATE_DISCOVERING  = 1,
    PS4SPLITSCREEN_STATE_CONNECTING   = 2,
    PS4SPLITSCREEN_STATE_CONNECTED    = 3,
    PS4SPLITSCREEN_STATE_SYNCING      = 4,
    PS4SPLITSCREEN_STATE_RUNNING      = 5,
    PS4SPLITSCREEN_STATE_DESYNCED     = 6,
    PS4SPLITSCREEN_STATE_ERROR        = 7,
} ps4splitscreen_state_t;

// =============================================================================
// Player/Controller
// =============================================================================
typedef struct {
    uint32_t player_id;         // 0 or 1
    uint32_t user_id;           // SceUserServiceUserId
    uint32_t pad_handle;        // ScePadHandle
    bool is_local;              // True if controlled on this PS4
} ps4splitscreen_player_t;

// =============================================================================
// Input State (synced between PS4s)
// =============================================================================
typedef struct {
    uint32_t frame_number;      // Frame this input belongs to
    uint32_t player_id;         // Which player
    uint32_t buttons_down;      // ScePad button bitmask
    uint32_t buttons_pressed;   // Pressed this frame
    uint32_t buttons_released;  // Released this frame
    int16_t left_stick_x;       // -32768 to 32767
    int16_t left_stick_y;
    int16_t right_stick_x;
    int16_t right_stick_y;
    uint16_t left_trigger;      // 0-255
    uint16_t right_trigger;     // 0-255
    uint64_t timestamp_us;      // Microseconds since session start
} ps4splitscreen_input_t;

// =============================================================================
// Game State (user-defined, synced via lockstep/rollback)
// =============================================================================
// Users should define their own game state structure and register
// serialization/deserialization callbacks.

typedef struct ps4splitscreen_game_state ps4splitscreen_game_state_t;

typedef void (*ps4splitscreen_state_serialize_fn)(const ps4splitscreen_game_state_t* state, uint8_t* buffer, size_t* size);
typedef void (*ps4splitscreen_state_deserialize_fn)(ps4splitscreen_game_state_t* state, const uint8_t* buffer, size_t size);
typedef void (*ps4splitscreen_state_free_fn)(ps4splitscreen_game_state_t* state);
typedef bool (*ps4splitscreen_state_equals_fn)(const ps4splitscreen_game_state_t* a, const ps4splitscreen_game_state_t* b);

// =============================================================================
// Synchronization Configuration
// =============================================================================
typedef enum {
    PS4SPLITSCREEN_SYNC_LOCKSTEP = 0,    // Deterministic lockstep
    PS4SPLITSCREEN_SYNC_ROLLBACK = 1,    // GGPO-style rollback
} ps4splitscreen_sync_mode_t;

typedef struct {
    ps4splitscreen_sync_mode_t mode;
    uint32_t lockstep_turn_timeout_ms;   // Lockstep: max wait per turn
    uint32_t rollback_max_frames;        // Rollback: max rewind frames
    uint32_t rollback_checkpoint_interval; // Rollback: save state every N frames
    bool enable_input_prediction;        // Rollback: predict local inputs
    uint32_t target_fps;                 // Target simulation FPS (usually 60)
} ps4splitscreen_sync_config_t;

static inline ps4splitscreen_sync_config_t ps4splitscreen_sync_config_default(void) {
    return (ps4splitscreen_sync_config_t){
        .mode = PS4SPLITSCREEN_SYNC_ROLLBACK,
        .lockstep_turn_timeout_ms = 100,
        .rollback_max_frames = 8,
        .rollback_checkpoint_interval = 4,
        .enable_input_prediction = true,
        .target_fps = 60,
    };
}

// =============================================================================
// Splitscreen Viewport Configuration
// =============================================================================
typedef enum {
    PS4SPLITSCREEN_LAYOUT_HORIZONTAL = 0,  // Side by side
    PS4SPLITSCREEN_LAYOUT_VERTICAL   = 1,  // Top/bottom
} ps4splitscreen_layout_t;

typedef struct {
    ps4splitscreen_layout_t layout;
    float player0_viewport[4];  // x, y, width, height (0.0-1.0 normalized)
    float player1_viewport[4];
    bool render_debug_overlay;  // Show latency, frame info
} ps4splitscreen_viewport_config_t;

static inline ps4splitscreen_viewport_config_t ps4splitscreen_viewport_config_default(ps4splitscreen_layout_t layout) {
    ps4splitscreen_viewport_config_t cfg = {
        .layout = layout,
        .render_debug_overlay = true,
    };
    if (layout == PS4SPLITSCREEN_LAYOUT_HORIZONTAL) {
        cfg.player0_viewport[0] = 0.0f; cfg.player0_viewport[1] = 0.0f;
        cfg.player0_viewport[2] = 0.5f; cfg.player0_viewport[3] = 1.0f;
        cfg.player1_viewport[0] = 0.5f; cfg.player1_viewport[1] = 0.0f;
        cfg.player1_viewport[2] = 0.5f; cfg.player1_viewport[3] = 1.0f;
    } else {
        cfg.player0_viewport[0] = 0.0f; cfg.player0_viewport[1] = 0.0f;
        cfg.player0_viewport[2] = 1.0f; cfg.player0_viewport[3] = 0.5f;
        cfg.player1_viewport[0] = 0.0f; cfg.player1_viewport[1] = 0.5f;
        cfg.player1_viewport[2] = 1.0f; cfg.player1_viewport[3] = 0.5f;
    }
    return cfg;
}

// =============================================================================
// Session Statistics
// =============================================================================
typedef struct {
    uint64_t frames_sent;
    uint64_t frames_received;
    uint64_t packets_sent;
    uint64_t packets_received;
    uint64_t packets_lost;
    uint64_t packets_reordered;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    float current_rtt_ms;
    float avg_rtt_ms;
    float jitter_ms;
    uint32_t local_frame;
    uint32_t remote_frame;
    int32_t frame_advantage;  // Positive = we're ahead
    uint32_t rollbacks_count;
    uint32_t desync_count;
} ps4splitscreen_stats_t;

// =============================================================================
// Callback Types
// =============================================================================
typedef void (*ps4splitscreen_on_connect_fn)(void* user_data, uint32_t remote_player_id);
typedef void (*ps4splitscreen_on_disconnect_fn)(void* user_data, uint32_t remote_player_id, int reason);
typedef void (*ps4splitscreen_on_desync_fn)(void* user_data, uint32_t frame, const char* details);
typedef void (*ps4splitscreen_on_state_request_fn)(void* user_data, uint32_t frame, ps4splitscreen_game_state_t** out_state);
typedef void (*ps4splitscreen_log_fn)(int level, const char* tag, const char* fmt, ...);

// =============================================================================
// Main Context (Opaque)
// =============================================================================
typedef struct ps4splitscreen_context ps4splitscreen_context_t;

// =============================================================================
// Lifecycle API
// =============================================================================

/**
 * @brief Initialize the splitscreen framework
 * @param net_config Network configuration (can be NULL for defaults)
 * @param sync_config Synchronization configuration (can be NULL for defaults)
 * @param viewport_config Viewport configuration (can be NULL for defaults)
 * @param log_callback Optional logging callback
 * @param user_data User data passed to callbacks
 * @return Initialized context or NULL on failure
 */
ps4splitscreen_context_t* ps4splitscreen_init(
    const ps4splitscreen_net_config_t* net_config,
    const ps4splitscreen_sync_config_t* sync_config,
    const ps4splitscreen_viewport_config_t* viewport_config,
    ps4splitscreen_log_fn log_callback,
    void* user_data
);

/**
 * @brief Shutdown and cleanup
 */
void ps4splitscreen_shutdown(ps4splitscreen_context_t* ctx);

/**
 * @brief Get current session state
 */
ps4splitscreen_state_t ps4splitscreen_get_state(const ps4splitscreen_context_t* ctx);

/**
 * @brief Get session role (host/client)
 */
ps4splitscreen_session_role_t ps4splitscreen_get_role(const ps4splitscreen_context_t* ctx);

// =============================================================================
// Session Management
// =============================================================================

/**
 * @brief Start hosting a session (call on one PS4)
 * @param session_name Human-readable session name (max 64 chars)
 * @param max_players Maximum players (1-2)
 * @return Error code
 */
int ps4splitscreen_host_session(ps4splitscreen_context_t* ctx, const char* session_name, uint32_t max_players);

/**
 * @brief Join a session by IP address
 * @param target_ip IPv4 address as string (e.g., "192.168.1.100")
 * @return Error code
 */
int ps4splitscreen_join_session(ps4splitscreen_context_t* ctx, const char* target_ip);

/**
 * @brief Start LAN discovery to find sessions
 * @param timeout_ms Discovery timeout
 * @param callback Called for each discovered session
 * @return Error code
 */
typedef void (*ps4splitscreen_discovery_cb)(void* user_data, const char* ip, const char* session_name, uint32_t players, uint32_t max_players);
int ps4splitscreen_discover_sessions(ps4splitscreen_context_t* ctx, uint32_t timeout_ms, ps4splitscreen_discovery_cb callback, void* user_data);

/**
 * @brief Leave current session
 */
void ps4splitscreen_leave_session(ps4splitscreen_context_t* ctx);

// =============================================================================
// Game State Registration (for synchronization)
// =============================================================================

/**
 * @brief Register game state serialization callbacks
 * @param state_size Size of game state structure
 * @param serialize Serialize state to buffer
 * @param deserialize Deserialize state from buffer
 * @param free_fn Optional free function for allocated state
 * @param equals_fn Optional equality check for desync detection
 */
void ps4splitscreen_register_game_state(
    ps4splitscreen_context_t* ctx,
    size_t state_size,
    ps4splitscreen_state_serialize_fn serialize,
    ps4splitscreen_state_deserialize_fn deserialize,
    ps4splitscreen_state_free_fn free_fn,
    ps4splitscreen_state_equals_fn equals_fn
);

// =============================================================================
// Main Loop
// =============================================================================

/**
 * @brief Process one frame - call once per frame
 * @param delta_time_ms Time since last frame
 * @param local_inputs Array of local player inputs (size = local player count)
 * @param input_count Number of local inputs
 * @return Error code, or frame number advanced
 */
int ps4splitscreen_tick(
    ps4splitscreen_context_t* ctx,
    float delta_time_ms,
    const ps4splitscreen_input_t* local_inputs,
    uint32_t input_count
);

/**
 * @brief Get the current synchronized game state (after tick)
 */
const ps4splitscreen_game_state_t* ps4splitscreen_get_game_state(const ps4splitscreen_context_t* ctx);

/**
 * @brief Get current frame number
 */
uint32_t ps4splitscreen_get_frame(const ps4splitscreen_context_t* ctx);

// =============================================================================
// Rendering (Splitscreen)
// =============================================================================

/**
 * @brief Begin splitscreen frame - sets up viewports for current player
 * @param ctx Context
 * @param player_index 0 or 1 (which player's view to render)
 * @return true if this player should be rendered on this PS4
 */
bool ps4splitscreen_begin_player_view(ps4splitscreen_context_t* ctx, uint32_t player_index);

/**
 * @brief End current player view
 */
void ps4splitscreen_end_player_view(ps4splitscreen_context_t* ctx);

/**
 * @brief Render debug overlay (latency, sync stats, etc.)
 */
void ps4splitscreen_render_debug_overlay(ps4splitscreen_context_t* ctx);

/**
 * @brief Get viewport config for manual rendering
 */
const ps4splitscreen_viewport_config_t* ps4splitscreen_get_viewport_config(const ps4splitscreen_context_t* ctx);

// =============================================================================
// Statistics & Diagnostics
// =============================================================================

/**
 * @brief Get current session statistics
 */
void ps4splitscreen_get_stats(const ps4splitscreen_context_t* ctx, ps4splitscreen_stats_t* out_stats);

/**
 * @brief Get local player ID (0 or 1)
 */
uint32_t ps4splitscreen_get_local_player_id(const ps4splitscreen_context_t* ctx);

/**
 * @brief Get remote player ID (0 or 1)
 */
uint32_t ps4splitscreen_get_remote_player_id(const ps4splitscreen_context_t* ctx);

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * @brief Convert error code to string
 */
const char* ps4splitscreen_strerror(int error_code);

/**
 * @brief Get version string
 */
const char* ps4splitscreen_version(void);

/**
 * @brief Set log level (0=none, 1=error, 2=warn, 3=info, 4=debug)
 */
void ps4splitscreen_set_log_level(int level);

#ifdef __cplusplus
}
#endif

#endif // PS4SPLITSCREEN_H