/**
 * @file ps4_discovery.c
 * @brief LAN Session Discovery Protocol
 * 
 * UDP broadcast-based discovery for finding splitscreen sessions on LAN.
 */

#include <ps4splitscreen/ps4splitscreen.h>
#include "ps4_socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// -----------------------------------------------------------------------------
// Discovery Protocol
// -----------------------------------------------------------------------------
#define DISCOVERY_MAGIC       0x50533453  // 'PS4S'
#define DISCOVERY_VERSION     1
#define DISCOVERY_MAX_NAME    64

typedef enum {
    DISCOVERY_MSG_QUERY    = 0x01,  // Client -> Broadcast: "Any sessions?"
    DISCOVERY_MSG_ANNOUNCE = 0x02,  // Host -> Broadcast: "I'm hosting"
    DISCOVERY_MSG_RESPONSE = 0x03,  // Host -> Client: Direct response
} discovery_msg_type_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t type;
    uint16_t reserved;
    uint32_t session_id;
    uint16_t game_port;
    uint16_t current_players;
    uint16_t max_players;
    char session_name[DISCOVERY_MAX_NAME];
    uint64_t timestamp_us;
} discovery_packet_t;

// -----------------------------------------------------------------------------
// Discovery Context
// -----------------------------------------------------------------------------
typedef struct {
    ps4_socket_t* socket;
    uint32_t session_id;
    uint16_t game_port;
    uint16_t current_players;
    uint16_t max_players;
    char session_name[DISCOVERY_MAX_NAME];
    bool is_hosting;
    uint64_t last_announce_us;
    uint32_t announce_interval_ms;
} discovery_context_t;

// -----------------------------------------------------------------------------
// Internal
// -----------------------------------------------------------------------------
static uint64_t get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void build_discovery_packet(discovery_context_t* ctx, discovery_msg_type_t type, discovery_packet_t* out) {
    memset(out, 0, sizeof(*out));
    out->magic = DISCOVERY_MAGIC;
    out->version = DISCOVERY_VERSION;
    out->type = type;
    out->session_id = ctx->session_id;
    out->game_port = ctx->game_port;
    out->current_players = ctx->current_players;
    out->max_players = ctx->max_players;
    strncpy(out->session_name, ctx->session_name, DISCOVERY_MAX_NAME - 1);
    out->timestamp_us = get_time_us();
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

discovery_context_t* ps4splitscreen_discovery_create(uint16_t discovery_port, uint16_t game_port, 
                                                      const char* session_name, uint32_t max_players) {
    discovery_context_t* ctx = calloc(1, sizeof(discovery_context_t));
    if (!ctx) return NULL;

    ctx->socket = ps4_socket_create(SOCKET_TYPE_UDP, 64*1024, 64*1024);
    if (!ctx->socket) {
        free(ctx);
        return NULL;
    }

    // Enable broadcast
    int broadcast = 1;
    sceNetSetsockopt(ctx->socket->fd, SCE_NET_SOL_SOCKET, SCE_NET_SO_BROADCAST, &broadcast, sizeof(broadcast));

    // Bind to discovery port
    int ret = ps4_socket_bind(ctx->socket, discovery_port, NULL);
    if (ret < 0) {
        ps4_socket_destroy(ctx->socket);
        free(ctx);
        return NULL;
    }

    // Set non-blocking
    ps4_socket_set_nonblocking(ctx->socket, true);

    ctx->session_id = (uint32_t)get_time_us();
    ctx->game_port = game_port;
    ctx->max_players = max_players;
    ctx->current_players = 1; // Host counts as 1
    ctx->announce_interval_ms = 2000;
    ctx->last_announce_us = 0;
    
    if (session_name) {
        strncpy(ctx->session_name, session_name, DISCOVERY_MAX_NAME - 1);
    } else {
        snprintf(ctx->session_name, DISCOVERY_MAX_NAME, "PS4Splitscreen_%08X", ctx->session_id);
    }

    return ctx;
}

void ps4splitscreen_discovery_destroy(discovery_context_t* ctx) {
    if (!ctx) return;
    if (ctx->socket) ps4_socket_destroy(ctx->socket);
    free(ctx);
}

int ps4splitscreen_discovery_start_hosting(discovery_context_t* ctx) {
    if (!ctx) return PS4SPLITSCREEN_ERR_INVALID_PARAM;
    ctx->is_hosting = true;
    ctx->last_announce_us = 0; // Force immediate announce
    return PS4SPLITSCREEN_OK;
}

void ps4splitscreen_discovery_stop_hosting(discovery_context_t* ctx) {
    if (!ctx) return;
    ctx->is_hosting = false;
}

int ps4splitscreen_discovery_update(discovery_context_t* ctx, 
                                     ps4splitscreen_discovery_cb callback, 
                                     void* user_data) {
    if (!ctx || !ctx->socket) return PS4SPLITSCREEN_ERR_INVALID_PARAM;

    uint64_t now = get_time_us();
    char broadcast_ip[16];
    ps4splitscreen_get_broadcast_ip(broadcast_ip, sizeof(broadcast_ip));

    // Send periodic announcements if hosting
    if (ctx->is_hosting && (now - ctx->last_announce_us) >= (uint64_t)ctx->announce_interval_ms * 1000) {
        discovery_packet_t packet;
        build_discovery_packet(ctx, DISCOVERY_MSG_ANNOUNCE, &packet);
        ps4_socket_sendto(ctx->socket, &packet, sizeof(packet), broadcast_ip, PS4SPLITSCREEN_DISCOVERY_PORT);
        ctx->last_announce_us = now;
    }

    // Process incoming packets
    discovery_packet_t packet;
    struct sockaddr_in src;
    while (true) {
        int received = ps4_socket_recv(ctx->socket, &packet, sizeof(packet), &src);
        if (received <= 0) break; // No more packets or error

        if (received < (int)sizeof(discovery_packet_t)) continue;
        if (packet.magic != DISCOVERY_MAGIC) continue;
        if (packet.version != DISCOVERY_VERSION) continue;

        char src_ip[16];
        sceNetInetNtop(SCE_NET_AF_INET, &src.sin_addr, src_ip, sizeof(src_ip));

        switch (packet.type) {
            case DISCOVERY_MSG_QUERY: {
                // Client looking for sessions - respond directly if hosting
                if (ctx->is_hosting) {
                    discovery_packet_t response;
                    build_discovery_packet(ctx, DISCOVERY_MSG_RESPONSE, &response);
                    ps4_socket_sendto(ctx->socket, &response, sizeof(response), src_ip, sceNetNtohs(src.sin_port));
                }
                break;
            }
            case DISCOVERY_MSG_ANNOUNCE:
            case DISCOVERY_MSG_RESPONSE: {
                // Found a session
                if (callback && packet.session_id != ctx->session_id) {
                    callback(user_data, src_ip, packet.session_name, 
                            packet.current_players, packet.max_players);
                }
                break;
            }
        }
    }

    return PS4SPLITSCREEN_OK;
}

int ps4splitscreen_discovery_query(discovery_context_t* ctx) {
    if (!ctx || !ctx->socket) return PS4SPLITSCREEN_ERR_INVALID_PARAM;

    char broadcast_ip[16];
    ps4splitscreen_get_broadcast_ip(broadcast_ip, sizeof(broadcast_ip));

    discovery_packet_t packet;
    build_discovery_packet(ctx, DISCOVERY_MSG_QUERY, &packet);
    
    return ps4_socket_sendto(ctx->socket, &packet, sizeof(packet), broadcast_ip, PS4SPLITSCREEN_DISCOVERY_PORT);
}

void ps4splitscreen_discovery_set_player_count(discovery_context_t* ctx, uint16_t current) {
    if (ctx) ctx->current_players = current;
}