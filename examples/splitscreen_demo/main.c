/**
 * @file main.c
 * @brief PS4 SplitScreen Connector - Example Game
 * 
 * Demonstrates a simple splitscreen game using the framework.
 * Two PS4s connect over LAN, each renders its own viewport.
 */

#include <ps4splitscreen/ps4splitscreen.h>
#include <ps4/gnm.h>
#include <ps4/gnmx.h>
#include <ps4/pad.h>
#include <ps4/kernel.h>
#include <ps4/debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// -----------------------------------------------------------------------------
// Game State
// -----------------------------------------------------------------------------
typedef struct {
    float player_pos[2][2];      // x, y for each player
    float player_vel[2][2];      // velocity
    uint32_t score[2];           // score
    uint32_t frame_number;       // current frame
    uint32_t game_mode;          // 0=menu, 1=playing, 2=paused
    float ball_pos[2];           // ball position
    float ball_vel[2];           // ball velocity
    uint32_t winner;             // 0=none, 1=player0, 2=player1
} game_state_t;

#define GAME_STATE_VERSION 1
#define ARENA_WIDTH 1920.0f
#define ARENA_HEIGHT 1080.0f
#define PLAYER_SPEED 300.0f
#define BALL_SPEED 400.0f

// -----------------------------------------------------------------------------
// Serialization
// -----------------------------------------------------------------------------
void game_state_serialize(const void* state, uint8_t* buffer, size_t* size) {
    const game_state_t* gs = (const game_state_t*)state;
    uint8_t* ptr = buffer;
    
    // Version
    *((uint32_t*)ptr) = GAME_STATE_VERSION; ptr += 4;
    
    // Player positions (8 floats = 32 bytes)
    memcpy(ptr, gs->player_pos, 2 * 2 * sizeof(float)); ptr += 32;
    
    // Player velocities
    memcpy(ptr, gs->player_vel, 2 * 2 * sizeof(float)); ptr += 32;
    
    // Scores
    *((uint32_t*)ptr) = gs->score[0]; ptr += 4;
    *((uint32_t*)ptr) = gs->score[1]; ptr += 4;
    
    // Frame
    *((uint32_t*)ptr) = gs->frame_number; ptr += 4;
    
    // Game mode
    *((uint32_t*)ptr) = gs->game_mode; ptr += 4;
    
    // Ball
    memcpy(ptr, gs->ball_pos, 2 * sizeof(float)); ptr += 8;
    memcpy(ptr, gs->ball_vel, 2 * sizeof(float)); ptr += 8;
    
    // Winner
    *((uint32_t*)ptr) = gs->winner; ptr += 4;
    
    *size = ptr - buffer;
}

void game_state_deserialize(void* state, const uint8_t* buffer, size_t size) {
    game_state_t* gs = (game_state_t*)state;
    const uint8_t* ptr = buffer;
    
    uint32_t version = *((const uint32_t*)ptr); ptr += 4;
    (void)version; // Could handle version migration
    
    memcpy(gs->player_pos, ptr, 32); ptr += 32;
    memcpy(gs->player_vel, ptr, 32); ptr += 32;
    gs->score[0] = *((const uint32_t*)ptr); ptr += 4;
    gs->score[1] = *((const uint32_t*)ptr); ptr += 4;
    gs->frame_number = *((const uint32_t*)ptr); ptr += 4;
    gs->game_mode = *((const uint32_t*)ptr); ptr += 4;
    memcpy(gs->ball_pos, ptr, 8); ptr += 8;
    memcpy(gs->ball_vel, ptr, 8); ptr += 8;
    gs->winner = *((const uint32_t*)ptr); ptr += 4;
}

bool game_state_equals(const void* a, const void* b) {
    const game_state_t* ga = (const game_state_t*)a;
    const game_state_t* gb = (const game_state_t*)b;
    return memcmp(ga, gb, sizeof(game_state_t)) == 0;
}

void game_state_free(void* state) {
    free(state);
}

// -----------------------------------------------------------------------------
// Game Logic
// -----------------------------------------------------------------------------
static game_state_t g_game_state = {0};
static ps4splitscreen_context_t* g_ctx = NULL;
static input_sync_t* g_input = NULL;
static splitscreen_viewport_t* g_viewport = NULL;
static framebuffer_manager_t* g_framebuffer = NULL;
static gnm_command_buffer_t* g_cmd_buffer = NULL;
static frame_timer_t* g_timer = NULL;

// Simple physics update (deterministic)
static void simulate_frame(void* user, uint32_t frame, const ps4splitscreen_input_t* inputs, int count) {
    (void)user;
    (void)frame;
    
    const float dt = 1.0f / 60.0f; // Fixed timestep
    
    // Process inputs for both players
    for (int p = 0; p < count && p < 2; p++) {
        const ps4splitscreen_input_t* in = &inputs[p];
        
        // Movement
        float move_x = ps4splitscreen_input_get_stick_x(in, true);
        float move_y = ps4splitscreen_input_get_stick_y(in, true);
        
        g_game_state.player_vel[p][0] = move_x * PLAYER_SPEED;
        g_game_state.player_vel[p][1] = move_y * PLAYER_SPEED;
        
        // Update position
        g_game_state.player_pos[p][0] += g_game_state.player_vel[p][0] * dt;
        g_game_state.player_pos[p][1] += g_game_state.player_vel[p][1] * dt;
        
        // Clamp to arena
        g_game_state.player_pos[p][0] = fmaxf(50.0f, fminf(ARENA_WIDTH - 50.0f, g_game_state.player_pos[p][0]));
        g_game_state.player_pos[p][1] = fmaxf(50.0f, fminf(ARENA_HEIGHT - 50.0f, g_game_state.player_pos[p][1]));
    }
    
    // Ball physics (only on host - frame 0)
    if (g_game_state.game_mode == 1) {
        g_game_state.ball_pos[0] += g_game_state.ball_vel[0] * dt;
        g_game_state.ball_pos[1] += g_game_state.ball_vel[1] * dt;
        
        // Ball collision with walls
        if (g_game_state.ball_pos[0] <= 20 || g_game_state.ball_pos[0] >= ARENA_WIDTH - 20) {
            g_game_state.ball_vel[0] = -g_game_state.ball_vel[0];
        }
        if (g_game_state.ball_pos[1] <= 20 || g_game_state.ball_pos[1] >= ARENA_HEIGHT - 20) {
            g_game_state.ball_vel[1] = -g_game_state.ball_vel[1];
        }
        
        // Ball collision with players (simple circle-circle)
        for (int p = 0; p < 2; p++) {
            float dx = g_game_state.ball_pos[0] - g_game_state.player_pos[p][0];
            float dy = g_game_state.ball_pos[1] - g_game_state.player_pos[p][1];
            float dist = sqrtf(dx*dx + dy*dy);
            
            if (dist < 60.0f && dist > 0.1f) {
                // Bounce
                float nx = dx / dist;
                float ny = dy / dist;
                float dot = g_game_state.ball_vel[0] * nx + g_game_state.ball_vel[1] * ny;
                if (dot < 0) {
                    g_game_state.ball_vel[0] -= 2 * dot * nx;
                    g_game_state.ball_vel[1] -= 2 * dot * ny;
                    // Add player velocity influence
                    g_game_state.ball_vel[0] += g_game_state.player_vel[p][0] * 0.3f;
                    g_game_state.ball_vel[1] += g_game_state.player_vel[p][1] * 0.3f;
                }
            }
        }
        
        // Scoring (ball goes off left/right)
        if (g_game_state.ball_pos[0] < 0) {
            g_game_state.score[1]++;
            g_game_state.ball_pos[0] = ARENA_WIDTH / 2;
            g_game_state.ball_pos[1] = ARENA_HEIGHT / 2;
            g_game_state.ball_vel[0] = BALL_SPEED;
            g_game_state.ball_vel[1] = 0;
        } else if (g_game_state.ball_pos[0] > ARENA_WIDTH) {
            g_game_state.score[0]++;
            g_game_state.ball_pos[0] = ARENA_WIDTH / 2;
            g_game_state.ball_pos[1] = ARENA_HEIGHT / 2;
            g_game_state.ball_vel[0] = -BALL_SPEED;
            g_game_state.ball_vel[1] = 0;
        }
        
        // Win check
        if (g_game_state.score[0] >= 5) g_game_state.winner = 1;
        else if (g_game_state.score[1] >= 5) g_game_state.winner = 2;
    }
    
    g_game_state.frame_number = frame;
}

// Save/load state for rollback
static void save_state(void* user, void* state_buffer) {
    (void)user;
    memcpy(state_buffer, &g_game_state, sizeof(game_state_t));
}

static void load_state(void* user, const void* state_buffer) {
    (void)user;
    memcpy(&g_game_state, state_buffer, sizeof(game_state_t));
}

// -----------------------------------------------------------------------------
// Rendering
// -----------------------------------------------------------------------------
static void render_player_view(uint32_t player_index) {
    // This would use GNM to render the game from player's perspective
    // For now, placeholder
    (void)player_index;
}

static void render_debug_ui(void) {
    // Render debug overlay with stats
    ps4splitscreen_stats_t stats;
    ps4splitscreen_get_stats(g_ctx, &stats);
    
    sceDbgPrintf("Frame: %u | RTT: %.1fms | Rollbacks: %u | Loss: %llu\n",
                 stats.local_frame, stats.current_rtt_ms, stats.rollbacks_count, stats.packets_lost);
}

// -----------------------------------------------------------------------------
// Callbacks
// -----------------------------------------------------------------------------
static void on_connect(void* user, uint32_t remote_player_id) {
    (void)user;
    sceDbgPrintf("Player %u connected!\n", remote_player_id);
    g_game_state.game_mode = 1; // Start game
    g_game_state.ball_pos[0] = ARENA_WIDTH / 2;
    g_game_state.ball_pos[1] = ARENA_HEIGHT / 2;
    g_game_state.ball_vel[0] = (ps4splitscreen_get_role(g_ctx) == PS4SPLITSCREEN_SESSION_HOST) ? BALL_SPEED : -BALL_SPEED;
    g_game_state.ball_vel[1] = 0;
}

static void on_disconnect(void* user, uint32_t remote_player_id, int reason) {
    (void)user;
    sceDbgPrintf("Player %u disconnected (reason: %d)\n", remote_player_id, reason);
    g_game_state.game_mode = 0; // Back to menu
}

static void on_desync(void* user, uint32_t frame, const char* details) {
    (void)user;
    sceDbgPrintf("DESYNC at frame %u: %s\n", frame, details);
}

static void log_callback(int level, const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    const char* level_str[] = {"", "ERROR", "WARN", "INFO", "DEBUG"};
    sceDbgPrintf("[%s] [%s] %s", level_str[level], tag, buf);
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    
    sceDbgPrintf("=== PS4 SplitScreen Connector Demo ===\n");
    
    // Initialize framework
    ps4splitscreen_net_config_t net_cfg = ps4splitscreen_net_config_default();
    ps4splitscreen_sync_config_t sync_cfg = ps4splitscreen_sync_config_default();
    sync_cfg.mode = PS4SPLITSCREEN_SYNC_ROLLBACK; // Use rollback netcode
    sync_cfg.rollback_max_frames = 8;
    ps4splitscreen_viewport_config_t vp_cfg = ps4splitscreen_viewport_config_default(PS4SPLITSCREEN_LAYOUT_HORIZONTAL);
    
    g_ctx = ps4splitscreen_init(&net_cfg, &sync_cfg, &vp_cfg, log_callback, NULL);
    if (!g_ctx) {
        sceDbgPrintf("Failed to initialize framework\n");
        return -1;
    }
    
    // Register game state
    ps4splitscreen_register_game_state(g_ctx, sizeof(game_state_t),
                                        game_state_serialize, game_state_deserialize,
                                        game_state_free, game_state_equals);
    
    // Set callbacks
    // Note: The session_manager.c has internal callbacks, would need to expose setters
    
    // Initialize input
    g_input = ps4splitscreen_input_create();
    if (!g_input) {
        sceDbgPrintf("Failed to initialize input\n");
        return -1;
    }
    
    // Initialize rendering
    g_viewport = ps4splitscreen_viewport_create(&vp_cfg, 1920, 1080);
    g_framebuffer = ps4splitscreen_framebuffer_create(1920, 1080, 
                                                       SCE_GNM_FORMAT_B8G8R8A8_UNORM,
                                                       SCE_GNM_FORMAT_D32_FLOAT_S8_UINT,
                                                       1, 2);
    g_cmd_buffer = ps4splitscreen_gnm_create_command_buffer(1024 * 1024);
    g_timer = ps4splitscreen_frame_timer_create(60);
    
    // Determine role from command line or default to host
    bool is_host = true; // Parse from argv in real app
    const char* target_ip = NULL; // Parse from argv
    
    int ret;
    if (is_host) {
        ret = ps4splitscreen_host_session(g_ctx, "Pong Demo", 2);
    } else {
        ret = ps4splitscreen_join_session(g_ctx, target_ip);
    }
    
    if (ret < 0) {
        sceDbgPrintf("Session failed: %s\n", ps4splitscreen_strerror(ret));
        return -1;
    }
    
    sceDbgPrintf("Session started! Role: %s\n", 
                 ps4splitscreen_get_role(g_ctx) == PS4SPLITSCREEN_SESSION_HOST ? "HOST" : "CLIENT");
    sceDbgPrintf("Local Player ID: %u\n", ps4splitscreen_get_local_player_id(g_ctx));
    
    // Main loop
    bool running = true;
    while (running) {
        // Frame timing
        ps4splitscreen_frame_timer_start_frame(g_timer);
        float dt = ps4splitscreen_frame_timer_get_delta(g_timer);
        
        // Update input
        ps4splitscreen_input_update(g_input);
        ps4splitscreen_input_set_frame(g_input, ps4splitscreen_get_frame(g_ctx), 
                                        g_timer->frame_start_us);
        
        // Check for exit (Options button)
        if (ps4splitscreen_input_is_pressed(g_input, PS4SPLITSCREEN_BUTTON_OPTIONS)) {
            running = false;
        }
        
        // Prepare local inputs
        ps4splitscreen_input_t local_inputs[2];
        uint32_t input_count = 0;
        uint32_t local_id = ps4splitscreen_get_local_player_id(g_ctx);
        
        // Our local input
        if (ps4splitscreen_input_is_connected(g_input, local_id)) {
            local_inputs[input_count] = *ps4splitscreen_input_get(g_input, local_id);
            local_inputs[input_count].player_id = local_id;
            local_inputs[input_count].frame_number = ps4splitscreen_get_frame(g_ctx);
            input_count++;
        }
        
        // Tick framework (handles networking + simulation)
        int frame_result = ps4splitscreen_tick(g_ctx, dt, local_inputs, input_count);
        if (frame_result < 0 && frame_result != PS4SPLITSCREEN_ERR_TIMEOUT) {
            sceDbgPrintf("Tick error: %s\n", ps4splitscreen_strerror(frame_result));
        }
        
        // Render
        if (g_cmd_buffer) {
            ps4splitscreen_gnm_begin_command_buffer(g_cmd_buffer);
            SceGnmCommandBuffer* cmd = ps4splitscreen_gnm_get_command_buffer(g_cmd_buffer);
            
            // Render each player's view
            for (int p = 0; p < 2; p++) {
                ps4splitscreen_viewport_begin_player(g_viewport, p, cmd);
                render_player_view(p);
                ps4splitscreen_viewport_end_player(g_viewport, cmd);
            }
            
            // Debug overlay
            ps4splitscreen_viewport_render_debug(g_viewport, cmd, 
                &(ps4splitscreen_stats_t){0}, local_id); // Stats fetched inside
            
            ps4splitscreen_gnm_end_command_buffer(g_cmd_buffer);
            
            // Submit command buffer (simplified)
            // sceGnmSubmitCommandBuffers(...);
            
            // Present
            ps4splitscreen_framebuffer_present(g_framebuffer, cmd);
            ps4splitscreen_framebuffer_advance(g_framebuffer);
        }
        
        ps4splitscreen_frame_timer_end_frame(g_timer);
        
        // Print stats occasionally
        static uint32_t stat_counter = 0;
        if (++stat_counter % 60 == 0) {
            render_debug_ui();
        }
    }
    
    // Cleanup
    sceDbgPrintf("Shutting down...\n");
    
    ps4splitscreen_leave_session(g_ctx);
    
    if (g_timer) ps4splitscreen_frame_timer_destroy(g_timer);
    if (g_cmd_buffer) ps4splitscreen_gnm_destroy_command_buffer(g_cmd_buffer);
    if (g_framebuffer) ps4splitscreen_framebuffer_destroy(g_framebuffer);
    if (g_viewport) ps4splitscreen_viewport_destroy(g_viewport);
    if (g_input) ps4splitscreen_input_destroy(g_input);
    if (g_ctx) ps4splitscreen_shutdown(g_ctx);
    
    sceDbgPrintf("Done.\n");
    return 0;
}