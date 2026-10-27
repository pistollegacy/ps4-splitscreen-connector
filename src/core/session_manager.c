/**
 * @file session_manager.c
 * @brief High-Level Session Manager
 * 
 * Orchestrates networking, synchronization, and rendering for splitscreen sessions.
 */

#include <ps4splitscreen/ps4splitscreen.h>
#include "ps4_net_init.h"
#include "ps4_socket.h"
#include "ps4_discovery.h"
#include "reliability_layer.h"
#include "packet_serialization.h"
#include "lockstep_sync.h"
#include "rollback_netcode.h"
#include "state_buffer.h"
#include "deterministic_rng.h"
#include "splitscreen_viewport.h"
#include "ps4_framebuffer.h"
#include "ps4_gnm_helpers.h"
#include <ps4/pad.h>
#include <ps4/kernel.h>
#include <ps4/thread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

// -----------------------------------------------------------------------------
// Internal Context
// -----------------------------------------------------------------------------
struct ps4splitscreen_context {
    // Configuration
    ps4splitscreen_net_config_t net_config;
    ps4splitscreen_sync_config_t sync_config;
    ps4splitscreen_viewport_config_t viewport_config;
    
    // Network
    ps4_socket_t* game_socket;
    ps4_socket_t* discovery_socket;
    discovery_context_t* discovery;
    reliability_context_t* reliability;
    struct sockaddr_in remote_addr;
    
    // Synchronization
    union {
        lockstep_context_t* lockstep;
        rollback_context_t* rollback;
    } sync;
    
    // State
    ps4splitscreen_state_t state;
    ps4splitscreen_session_role_t role;
    uint32_t local_player_id;
    uint32_t remote_player_id;
    uint32_t max_players;
    
    // Game state callbacks
    size_t game_state_size;
    ps4splitscreen_state_serialize_fn serialize_fn;
    ps4splitscreen_state_deserialize_fn deserialize_fn;
    ps4splitscreen_state_free_fn free_fn;
    ps4splitscreen_state_equals_fn equals_fn;
    void* game_state_buffer;
    
    // Rendering
    splitscreen_viewport_t* viewport;
    framebuffer_manager_t* framebuffer;
    
    // Input
    ps4splitscreen_input_t local_inputs[2];
    uint32_t local_input_count;
    
    // Callbacks
    ps4splitscreen_on_connect_fn on_connect;
    ps4splitscreen_on_disconnect_fn on_disconnect;
    ps4splitscreen_on_desync_fn on_desync;
    ps4splitscreen_on_state_request_fn on_state_request;
    ps4splitscreen_log_fn log_fn;
    void* user_data;
    
    // Threading
    pthread_t network_thread;
    bool network_thread_running;
    pthread_mutex_t mutex;
    
    // Stats
    ps4splitscreen_stats_t stats;
    uint64_t session_start_us;
    uint64_t last_frame_us;
    
    // RNG
    deterministic_rng_t* rng;
};

// -----------------------------------------------------------------------------
// Logging
// -----------------------------------------------------------------------------
static void log_msg(ps4splitscreen_context_t* ctx, int level, const char* tag, const char* fmt, ...) {
    if (ctx && ctx->log_fn) {
        va_list args;
        va_start(args, fmt);
        ctx->log_fn(level, tag, fmt, args);
        va_end(args);
    }
}

#define LOG_DEBUG(ctx, tag, ...) log_msg(ctx, 4, tag, __VA_ARGS__)
#define LOG_INFO(ctx, tag, ...)  log_msg(ctx, 3, tag, __VA_ARGS__)
#define LOG_WARN(ctx, tag, ...)  log_msg(ctx, 2, tag, __VA_ARGS__)
#define LOG_ERROR(ctx, tag, ...) log_msg(ctx, 1, tag, __VA_ARGS__)

// -----------------------------------------------------------------------------
// Network Thread
// -----------------------------------------------------------------------------
static void* network_thread_func(void* arg) {
    ps4splitscreen_context_t* ctx = (ps4splitscreen_context_t*)arg;
    
    uint8_t recv_buffer[PS4SPLITSCREEN_MAX_PACKET_SIZE];
    
    while (ctx->network_thread_running) {
        pthread_mutex_lock(&ctx->mutex);
        
        // Receive packets
        struct sockaddr_in src;
        int received = ps4_socket_recv(ctx->game_socket, recv_buffer, sizeof(recv_buffer), &src);
        
        if (received > 0) {
            // Verify packet
            if (ps4splitscreen_packet_verify(recv_buffer, received)) {
                packet_type_t type = ps4splitscreen_packet_get_type(recv_buffer, received);
                uint16_t seq = ps4splitscreen_packet_get_sequence(recv_buffer, received);
                uint32_t frame = ps4splitscreen_packet_get_frame(recv_buffer, received);
                uint16_t payload_size;
                const uint8_t* payload = ps4splitscreen_packet_get_payload(recv_buffer, received, &payload_size);
                
                // Process packet based on type
                switch (type) {
                    case PKT_INPUT: {
                        if (payload_size >= 32) {
                            ps4splitscreen_input_t input;
                            ps4splitscreen_deserialize_input(payload, payload_size, &input);
                            
                            if (ctx->sync_config.mode == PS4SPLITSCREEN_SYNC_LOCKSTEP) {
                                ps4splitscreen_lockstep_receive_input(ctx->sync.lockstep, &input);
                            } else {
                                ps4splitscreen_rollback_add_remote_input(ctx->sync.rollback, &input);
                            }
                            ctx->stats.packets_received++;
                            ctx->stats.bytes_received += received;
                        }
                        break;
                    }
                    case PKT_STATE: {
                        // Full state sync (for checkpoint/restore)
                        if (ctx->deserialize_fn && ctx->game_state_buffer) {
                            ctx->deserialize_fn(ctx->game_state_buffer, payload, payload_size);
                        }
                        break;
                    }
                    case PKT_STATE_REQUEST: {
                        // Peer requests state - send current state
                        if (ctx->serialize_fn && ctx->game_state_buffer) {
                            uint8_t state_buf[4096];
                            size_t state_size = 0;
                            ctx->serialize_fn(ctx->game_state_buffer, state_buf, &state_size);
                            
                            uint8_t* pkt = ps4splitscreen_packet_create(PKT_STATE, seq, frame, 
                                                                         state_buf, state_size, NULL);
                            if (pkt) {
                                ps4_socket_send(ctx->game_socket, pkt, PACKET_HEADER_SIZE + state_size);
                                free(pkt);
                            }
                        }
                        break;
                    }
                    case PKT_GOODBYE: {
                        ctx->state = PS4SPLITSCREEN_STATE_DISCONNECTED;
                        if (ctx->on_disconnect) {
                            ctx->on_disconnect(ctx->user_data, ctx->remote_player_id, 0);
                        }
                        break;
                    }
                }
            }
        }
        
        // Update reliability layer
        if (ctx->reliability) {
            ps4splitscreen_reliability_update(ctx->reliability);
        }
        
        // Update discovery
        if (ctx->discovery && ctx->role == PS4SPLITSCREEN_SESSION_HOST) {
            ps4splitscreen_discovery_update(ctx->discovery, NULL, NULL);
        }
        
        pthread_mutex_unlock(&ctx->mutex);
        
        // Small sleep to avoid busy loop
        sceKernelUsleep(1000); // 1ms
    }
    
    return NULL;
}

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

ps4splitscreen_context_t* ps4splitscreen_init(
    const ps4splitscreen_net_config_t* net_config,
    const ps4splitscreen_sync_config_t* sync_config,
    const ps4splitscreen_viewport_config_t* viewport_config,
    ps4splitscreen_log_fn log_callback,
    void* user_data) {
    
    // Initialize network stack
    int ret = ps4splitscreen_net_init();
    if (ret < 0) return NULL;
    
    ps4splitscreen_context_t* ctx = calloc(1, sizeof(ps4splitscreen_context_t));
    if (!ctx) return NULL;
    
    // Copy configs
    ctx->net_config = net_config ? *net_config : ps4splitscreen_net_config_default();
    ctx->sync_config = sync_config ? *sync_config : ps4splitscreen_sync_config_default();
    ctx->viewport_config = viewport_config ? *viewport_config : ps4splitscreen_viewport_config_default(PS4SPLITSCREEN_LAYOUT_HORIZONTAL);
    
    ctx->log_fn = log_callback;
    ctx->user_data = user_data;
    ctx->state = PS4SPLITSCREEN_STATE_DISCONNECTED;
    ctx->role = PS4SPLITSCREEN_SESSION_HOST; // Default, changed on join
    ctx->session_start_us = 0;
    ctx->last_frame_us = 0;
    
    pthread_mutex_init(&ctx->mutex, NULL);
    
    // Create game socket (UDP)
    ctx->game_socket = ps4_socket_create(SOCKET_TYPE_UDP, 
                                          ctx->net_config.send_buffer_size,
                                          ctx->net_config.recv_buffer_size);
    if (!ctx->game_socket) {
        ps4splitscreen_shutdown(ctx);
        return NULL;
    }
    
    // Bind to game port
    ret = ps4_socket_bind(ctx->game_socket, ctx->net_config.game_port, NULL);
    if (ret < 0) {
        ps4splitscreen_shutdown(ctx);
        return NULL;
    }
    
    // Set non-blocking
    ps4_socket_set_nonblocking(ctx->game_socket, true);
    
    // Create reliability layer
    memset(&ctx->remote_addr, 0, sizeof(ctx->remote_addr));
    ctx->remote_addr.sin_family = SCE_NET_AF_INET;
    ctx->remote_addr.sin_port = sceNetHtons(ctx->net_config.game_port);
    ctx->remote_addr.sin_addr.s_addr = SCE_NET_INADDR_ANY;
    
    ctx->reliability = ps4splitscreen_reliability_create(ctx->game_socket, &ctx->remote_addr, 4);
    
    // Create discovery
    ctx->discovery = ps4splitscreen_discovery_create(ctx->net_config.discovery_port,
                                                      ctx->net_config.game_port,
                                                      NULL, 2);
    
    // Create viewport
    ctx->viewport = ps4splitscreen_viewport_create(&ctx->viewport_config, 1920, 1080);
    
    // Create RNG
    uint64_t seed = (uint64_t)time(NULL) ^ (uint64_t)sceKernelGetProcessId();
    ctx->rng = ps4splitscreen_rng_create(seed, 0x50533453, false);
    
    LOG_INFO(ctx, "PS4SplitScreen", "Initialized successfully");
    
    return ctx;
}

void ps4splitscreen_shutdown(ps4splitscreen_context_t* ctx) {
    if (!ctx) return;
    
    // Stop network thread
    ctx->network_thread_running = false;
    if (ctx->network_thread) {
        pthread_join(ctx->network_thread, NULL);
    }
    
    // Send goodbye if connected
    if (ctx->state == PS4SPLITSCREEN_STATE_CONNECTED || 
        ctx->state == PS4SPLITSCREEN_STATE_RUNNING) {
        uint8_t* pkt = ps4splitscreen_packet_create(PKT_GOODBYE, 0, 0, NULL, 0, NULL);
        if (pkt) {
            ps4_socket_send(ctx->game_socket, pkt, PACKET_HEADER_SIZE);
            free(pkt);
        }
    }
    
    // Cleanup
    if (ctx->sync.lockstep) ps4splitscreen_lockstep_destroy(ctx->sync.lockstep);
    if (ctx->sync.rollback) ps4splitscreen_rollback_destroy(ctx->sync.rollback);
    if (ctx->reliability) ps4splitscreen_reliability_destroy(ctx->reliability);
    if (ctx->discovery) ps4splitscreen_discovery_destroy(ctx->discovery);
    if (ctx->game_socket) ps4_socket_destroy(ctx->game_socket);
    if (ctx->viewport) ps4splitscreen_viewport_destroy(ctx->viewport);
    if (ctx->framebuffer) ps4splitscreen_framebuffer_destroy(ctx->framebuffer);
    if (ctx->rng) ps4splitscreen_rng_destroy(ctx->rng);
    if (ctx->game_state_buffer && ctx->free_fn) ctx->free_fn(ctx->game_state_buffer);
    
    pthread_mutex_destroy(&ctx->mutex);
    
    ps4splitscreen_net_term();
    
    free(ctx);
}

ps4splitscreen_state_t ps4splitscreen_get_state(const ps4splitscreen_context_t* ctx) {
    return ctx ? ctx->state : PS4SPLITSCREEN_STATE_ERROR;
}

ps4splitscreen_session_role_t ps4splitscreen_get_role(const ps4splitscreen_context_t* ctx) {
    return ctx ? ctx->role : PS4SPLITSCREEN_SESSION_HOST;
}

// -----------------------------------------------------------------------------
// Session Management
// -----------------------------------------------------------------------------

int ps4splitscreen_host_session(ps4splitscreen_context_t* ctx, const char* session_name, uint32_t max_players) {
    if (!ctx) return PS4SPLITSCREEN_ERR_INVALID_PARAM;
    if (max_players < 1 || max_players > PS4SPLITSCREEN_MAX_PLAYERS) return PS4SPLITSCREEN_ERR_INVALID_PARAM;
    
    pthread_mutex_lock(&ctx->mutex);
    
    ctx->role = PS4SPLITSCREEN_SESSION_HOST;
    ctx->local_player_id = 0;
    ctx->remote_player_id = 1;
    ctx->max_players = max_players;
    ctx->state = PS4SPLITSCREEN_STATE_DISCOVERING;
    
    // Update discovery
    if (ctx->discovery) {
        if (session_name) strncpy(ctx->discovery->session_name, session_name, DISCOVERY_MAX_NAME - 1);
        ctx->discovery->max_players = max_players;
        ctx->discovery->current_players = 1;
        ps4splitscreen_discovery_start_hosting(ctx->discovery);
    }
    
    // Initialize sync
    if (ctx->sync_config.mode == PS4SPLITSCREEN_SYNC_LOCKSTEP) {
        ctx->sync.lockstep = ps4splitscreen_lockstep_create(ctx->local_player_id, ctx->remote_player_id, 
                                                              ctx->sync_config.lockstep_turn_timeout_ms);
    } else {
        ctx->sync.rollback = ps4splitscreen_rollback_create(ctx->local_player_id, ctx->remote_player_id,
                                                               ctx->game_state_size,
                                                               ctx->sync_config.rollback_max_frames,
                                                               ctx->sync_config.rollback_checkpoint_interval,
                                                               ctx->sync_config.enable_input_prediction);
        // Set rollback callbacks
        ps4splitscreen_rollback_set_callbacks(ctx->sync.rollback,
            // simulate
            [](void* user, uint32_t frame, const ps4splitscreen_input_t* inputs, int count) {
                ps4splitscreen_context_t* ctx = (ps4splitscreen_context_t*)user;
                // Game logic simulation would go here
            },
            // save_state
            [](void* user, void* state_buffer) {
                ps4splitscreen_context_t* ctx = (ps4splitscreen_context_t*)user;
                if (ctx->serialize_fn && ctx->game_state_buffer) {
                    size_t size = 0;
                    ctx->serialize_fn(ctx->game_state_buffer, state_buffer, &size);
                }
            },
            // load_state
            [](void* user, const void* state_buffer) {
                ps4splitscreen_context_t* ctx = (ps4splitscreen_context_t*)user;
                if (ctx->deserialize_fn && ctx->game_state_buffer) {
                    ctx->deserialize_fn(ctx->game_state_buffer, state_buffer, ctx->game_state_size);
                }
            },
            // free_state
            [](void* state_buffer) { free(state_buffer); },
            ctx);
    }
    
    // Start network thread
    ctx->network_thread_running = true;
    pthread_create(&ctx->network_thread, NULL, network_thread_func, ctx);
    
    ctx->state = PS4SPLITSCREEN_STATE_CONNECTED;
    ctx->session_start_us = get_time_us();
    
    LOG_INFO(ctx, "Session", "Hosting session '%s' for %u players", 
             session_name ? session_name : "unnamed", max_players);
    
    pthread_mutex_unlock(&ctx->mutex);
    return PS4SPLITSCREEN_OK;
}

int ps4splitscreen_join_session(ps4splitscreen_context_t* ctx, const char* target_ip) {
    if (!ctx || !target_ip) return PS4SPLITSCREEN_ERR_INVALID_PARAM;
    
    pthread_mutex_lock(&ctx->mutex);
    
    ctx->role = PS4SPLITSCREEN_SESSION_CLIENT;
    ctx->local_player_id = 1; // Client is player 1
    ctx->remote_player_id = 0; // Host is player 0
    ctx->state = PS4SPLITSCREEN_STATE_CONNECTING;
    
    // Set remote address
    ctx->remote_addr.sin_addr.s_addr = 0;
    sceNetInetPton(SCE_NET_AF_INET, target_ip, &ctx->remote_addr.sin_addr);
    ctx->remote_addr.sin_port = sceNetHtons(ctx->net_config.game_port);
    
    // Update reliability with remote address
    if (ctx->reliability) {
        ps4splitscreen_reliability_destroy(ctx->reliability);
    }
    ctx->reliability = ps4splitscreen_reliability_create(ctx->game_socket, &ctx->remote_addr, 4);
    
    // Send HELLO packet
    uint8_t* hello = ps4splitscreen_build_hello(0, 0, 1, 2, ctx->sync_config.mode, "token", NULL);
    if (hello) {
        size_t hello_size = PACKET_HEADER_SIZE + sizeof(hello_payload_t);
        ps4_socket_sendto(ctx->game_socket, hello, hello_size, target_ip, ctx->net_config.game_port);
        free(hello);
    }
    
    // Start network thread
    ctx->network_thread_running = true;
    pthread_create(&ctx->network_thread, NULL, network_thread_func, ctx);
    
    // Wait for WELCOME (with timeout)
    uint64_t start = get_time_us();
    while (ctx->state == PS4SPLITSCREEN_STATE_CONNECTING) {
        pthread_mutex_unlock(&ctx->mutex);
        sceKernelUsleep(10000); // 10ms
        pthread_mutex_lock(&ctx->mutex);
        
        if ((get_time_us() - start) > ctx->net_config.connect_timeout_ms * 1000) {
            ctx->state = PS4SPLITSCREEN_STATE_ERROR;
            pthread_mutex_unlock(&ctx->mutex);
            return PS4SPLITSCREEN_ERR_TIMEOUT;
        }
    }
    
    pthread_mutex_unlock(&ctx->mutex);
    return PS4SPLITSCREEN_OK;
}

int ps4splitscreen_discover_sessions(ps4splitscreen_context_t* ctx, uint32_t timeout_ms, 
                                      ps4splitscreen_discovery_cb callback, void* user_data) {
    if (!ctx || !ctx->discovery) return PS4SPLITSCREEN_ERR_INVALID_PARAM;
    
    // Send query
    ps4splitscreen_discovery_query(ctx->discovery);
    
    // Wait for responses
    uint64_t start = get_time_us();
    while ((get_time_us() - start) < (uint64_t)timeout_ms * 1000) {
        ps4splitscreen_discovery_update(ctx->discovery, callback, user_data);
        sceKernelUsleep(10000);
    }
    
    return PS4SPLITSCREEN_OK;
}

void ps4splitscreen_leave_session(ps4splitscreen_context_t* ctx) {
    if (!ctx) return;
    ps4splitscreen_shutdown(ctx);
}

// -----------------------------------------------------------------------------
// Game State Registration
// -----------------------------------------------------------------------------

void ps4splitscreen_register_game_state(
    ps4splitscreen_context_t* ctx,
    size_t state_size,
    ps4splitscreen_state_serialize_fn serialize,
    ps4splitscreen_state_deserialize_fn deserialize,
    ps4splitscreen_state_free_fn free_fn,
    ps4splitscreen_state_equals_fn equals_fn) {
    
    if (!ctx) return;
    
    ctx->game_state_size = state_size;
    ctx->serialize_fn = serialize;
    ctx->deserialize_fn = deserialize;
    ctx->free_fn = free_fn;
    ctx->equals_fn = equals_fn;
    
    if (state_size > 0) {
        ctx->game_state_buffer = malloc(state_size);
        memset(ctx->game_state_buffer, 0, state_size);
    }
}

// -----------------------------------------------------------------------------
// Main Loop
// -----------------------------------------------------------------------------

int ps4splitscreen_tick(ps4splitscreen_context_t* ctx, float delta_time_ms,
                         const ps4splitscreen_input_t* local_inputs, uint32_t input_count) {
    if (!ctx) return PS4SPLITSCREEN_ERR_INVALID_PARAM;
    
    pthread_mutex_lock(&ctx->mutex);
    
    uint64_t now = get_time_us();
    uint32_t current_frame = 0;
    
    // Store local inputs
    for (uint32_t i = 0; i < input_count && i < 2; i++) {
        ctx->local_inputs[i] = local_inputs[i];
        ctx->local_inputs[i].frame_number = ctx->stats.local_frame;
        ctx->local_inputs[i].timestamp_us = now - ctx->session_start_us;
    }
    ctx->local_input_count = input_count;
    
    // Process based on sync mode
    int result = PS4SPLITSCREEN_OK;
    
    if (ctx->sync_config.mode == PS4SPLITSCREEN_SYNC_LOCKSTEP) {
        // Submit local input
        for (uint32_t i = 0; i < input_count; i++) {
            ps4splitscreen_lockstep_submit_input(ctx->sync.lockstep, &ctx->local_inputs[i]);
        }
        
        // Update lockstep
        ps4splitscreen_lockstep_update(ctx->sync.lockstep);
        
        // Check if we can advance
        if (ps4splitscreen_lockstep_can_advance(ctx->sync.lockstep)) {
            result = ps4splitscreen_lockstep_advance(ctx->sync.lockstep);
            ctx->stats.local_frame = ctx->sync.lockstep->current_frame;
        }
        
        current_frame = ps4splitscreen_lockstep_get_frame(ctx->sync.lockstep);
        
    } else {
        // Rollback mode
        // Add local input
        for (uint32_t i = 0; i < input_count; i++) {
            ps4splitscreen_rollback_add_local_input(ctx->sync.rollback, &ctx->local_inputs[i]);
        }
        
        // Advance frame
        result = ps4splitscreen_rollback_advance(ctx->sync.rollback);
        ctx->stats.local_frame = ctx->sync.rollback->current_frame;
        ctx->stats.rollbacks_count = ps4splitscreen_rollback_get_rollback_count(ctx->sync.rollback);
        
        current_frame = ps4splitscreen_rollback_get_frame(ctx->sync.rollback);
    }
    
    // Send local inputs to remote
    for (uint32_t i = 0; i < input_count; i++) {
        uint8_t input_buf[32];
        size_t input_size = ps4splitscreen_serialize_input(&ctx->local_inputs[i], input_buf, sizeof(input_buf));
        
        uint8_t* pkt = ps4splitscreen_packet_create(PKT_INPUT, ctx->stats.packets_sent, 
                                                     current_frame, input_buf, input_size, NULL);
        if (pkt) {
            size_t pkt_size = PACKET_HEADER_SIZE + input_size;
            ps4_socket_send(ctx->game_socket, pkt, pkt_size);
            free(pkt);
            ctx->stats.packets_sent++;
            ctx->stats.bytes_sent += pkt_size;
        }
    }
    
    // Update stats
    ctx->stats.current_rtt_ms = ps4splitscreen_reliability_get_rtt(ctx->reliability);
    ctx->stats.remote_frame = current_frame; // Simplified
    ctx->stats.frame_advantage = (int32_t)ctx->stats.local_frame - (int32_t)ctx->stats.remote_frame;
    ctx->last_frame_us = now;
    
    // State update callback
    if (ctx->state == PS4SPLITSCREEN_STATE_CONNECTED) {
        ctx->state = PS4SPLITSCREEN_STATE_RUNNING;
    }
    
    pthread_mutex_unlock(&ctx->mutex);
    
    return result;
}

const ps4splitscreen_game_state_t* ps4splitscreen_get_game_state(const ps4splitscreen_context_t* ctx) {
    return ctx ? (const ps4splitscreen_game_state_t*)ctx->game_state_buffer : NULL;
}

uint32_t ps4splitscreen_get_frame(const ps4splitscreen_context_t* ctx) {
    return ctx ? ctx->stats.local_frame : 0;
}

// -----------------------------------------------------------------------------
// Rendering
// -----------------------------------------------------------------------------

bool ps4splitscreen_begin_player_view(ps4splitscreen_context_t* ctx, uint32_t player_index) {
    if (!ctx || !ctx->viewport) return false;
    // Note: Requires command buffer from caller
    return true; // Actual begin happens with cmd buffer
}

void ps4splitscreen_end_player_view(ps4splitscreen_context_t* ctx) {
    // Handled by viewport end
}

void ps4splitscreen_render_debug_overlay(ps4splitscreen_context_t* ctx) {
    // Requires command buffer
}

const ps4splitscreen_viewport_config_t* ps4splitscreen_get_viewport_config(const ps4splitscreen_context_t* ctx) {
    return ctx ? &ctx->viewport_config : NULL;
}

// -----------------------------------------------------------------------------
// Statistics
// -----------------------------------------------------------------------------

void ps4splitscreen_get_stats(const ps4splitscreen_context_t* ctx, ps4splitscreen_stats_t* out_stats) {
    if (!ctx || !out_stats) return;
    *out_stats = ctx->stats;
}

uint32_t ps4splitscreen_get_local_player_id(const ps4splitscreen_context_t* ctx) {
    return ctx ? ctx->local_player_id : 0;
}

uint32_t ps4splitscreen_get_remote_player_id(const ps4splitscreen_context_t* ctx) {
    return ctx ? ctx->remote_player_id : 0;
}

// -----------------------------------------------------------------------------
// Utilities
// -----------------------------------------------------------------------------

const char* ps4splitscreen_strerror(int error_code) {
    switch (error_code) {
        case PS4SPLITSCREEN_OK: return "OK";
        case PS4SPLITSCREEN_ERR_INIT_FAILED: return "Initialization failed";
        case PS4SPLITSCREEN_ERR_NET_INIT_FAILED: return "Network initialization failed";
        case PS4SPLITSCREEN_ERR_SOCKET_FAILED: return "Socket creation failed";
        case PS4SPLITSCREEN_ERR_BIND_FAILED: return "Socket bind failed";
        case PS4SPLITSCREEN_ERR_CONNECT_FAILED: return "Connection failed";
        case PS4SPLITSCREEN_ERR_SEND_FAILED: return "Send failed";
        case PS4SPLITSCREEN_ERR_RECV_FAILED: return "Receive failed";
        case PS4SPLITSCREEN_ERR_TIMEOUT: return "Timeout";
        case PS4SPLITSCREEN_ERR_INVALID_PARAM: return "Invalid parameter";
        case PS4SPLITSCREEN_ERR_OUT_OF_MEMORY: return "Out of memory";
        case PS4SPLITSCREEN_ERR_NOT_CONNECTED: return "Not connected";
        case PS4SPLITSCREEN_ERR_ALREADY_CONNECTED: return "Already connected";
        case PS4SPLITSCREEN_ERR_PROTOCOL_MISMATCH: return "Protocol mismatch";
        case PS4SPLITSCREEN_ERR_DESYNC_DETECTED: return "Desync detected";
        case PS4SPLITSCREEN_ERR_GNM_FAILED: return "GNM error";
        case PS4SPLITSCREEN_ERR_VIDEO_OUT_FAILED: return "Video output error";
        default: return "Unknown error";
    }
}

const char* ps4splitscreen_version(void) {
    return PS4SPLITSCREEN_VERSION_STRING;
}

void ps4splitscreen_set_log_level(int level) {
    // Global log level - would need a global variable
    (void)level;
}

// Helper for time
static uint64_t get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}