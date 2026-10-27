/**
 * @file reliability_layer.c
 * @brief Reliable UDP Layer with Sequencing, ACKs, and Retransmission
 * 
 * Provides reliable, ordered delivery over UDP for game state synchronization.
 */

#include <ps4splitscreen/ps4splitscreen.h>
#include "ps4_socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// -----------------------------------------------------------------------------
// Protocol Constants
// -----------------------------------------------------------------------------
#define RELIABILITY_WINDOW_SIZE     64
#define RELIABILITY_MAX_RETRIES     5
#define RELIABILITY_BASE_RTO_MS     50   // Base retransmission timeout
#define RELIABILITY_MAX_RTO_MS      1000
#define RELIABILITY_ACK_DELAY_MS    10   // Delayed ACK timer
#define RELIABILITY_KEEPALIVE_MS    1000

#define REL_FLAG_NONE      0x00
#define REL_FLAG_ACK       0x01
#define REL_FLAG_SYN       0x02
#define REL_FLAG_FIN       0x04
#define REL_FLAG_RST       0x08

// -----------------------------------------------------------------------------
// Packet Structures
// -----------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint32_t magic;           // 0x52454C59 ('RELY')
    uint16_t sequence;        // Packet sequence number
    uint16_t ack_sequence;    // Acknowledged sequence
    uint16_t ack_bitfield;    // Bitfield of recent received sequences
    uint8_t flags;            // REL_FLAG_*
    uint8_t channel;          // Channel ID (0-3)
    uint16_t payload_size;    // Size of payload
    uint32_t timestamp_us;    // Send timestamp
    uint8_t payload[];        // Variable payload
} reliability_packet_t;

#define RELIABILITY_HEADER_SIZE (sizeof(reliability_packet_t) - sizeof(((reliability_packet_t*)0)->payload))

// -----------------------------------------------------------------------------
// Channel State
// -----------------------------------------------------------------------------
typedef struct {
    uint16_t local_sequence;      // Next sequence to send
    uint16_t remote_sequence;     // Highest received sequence
    uint16_t ack_sequence;        // Highest acknowledged sequence
    uint64_t ack_bitfield;        // 64-bit bitfield for SACK
    
    // Send buffer (circular)
    struct {
        reliability_packet_t* packet;
        uint64_t send_time_us;
        uint8_t retries;
        bool pending;
    } send_buffer[RELIABILITY_WINDOW_SIZE];
    
    // Receive buffer (for reordering)
    struct {
        reliability_packet_t* packet;
        bool valid;
    } recv_buffer[RELIABILITY_WINDOW_SIZE];
    
    // Callbacks
    void (*on_data)(void* user, uint8_t channel, const uint8_t* data, size_t size);
    void* user_data;
    
    // Stats
    uint64_t packets_sent;
    uint64_t packets_received;
    uint64_t packets_retransmitted;
    uint64_t packets_lost;
    uint64_t bytes_sent;
    uint64_t bytes_received;
} reliability_channel_t;

typedef struct {
    ps4_socket_t* socket;
    struct sockaddr_in remote_addr;
    reliability_channel_t channels[4];
    uint32_t channel_count;
    uint64_t last_ack_us;
    uint64_t last_keepalive_us;
    float smoothed_rtt_ms;
    float rtt_variance_ms;
    bool connected;
} reliability_context_t;

// -----------------------------------------------------------------------------
// Internal Helpers
// -----------------------------------------------------------------------------
static uint64_t get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static uint16_t seq_diff(uint16_t newer, uint16_t older) {
    return (uint16_t)(newer - older);
}

static bool seq_greater(uint16_t a, uint16_t b) {
    return seq_diff(a, b) < 32768;
}

static reliability_packet_t* alloc_packet(size_t payload_size) {
    reliability_packet_t* pkt = malloc(RELIABILITY_HEADER_SIZE + payload_size);
    return pkt;
}

static void free_packet(reliability_packet_t* pkt) {
    free(pkt);
}

static void update_rtt(reliability_context_t* ctx, float rtt_ms) {
    if (ctx->smoothed_rtt_ms == 0) {
        ctx->smoothed_rtt_ms = rtt_ms;
        ctx->rtt_variance_ms = rtt_ms / 2;
    } else {
        ctx->rtt_variance_ms = ctx->rtt_variance_ms * 0.75f + 
                               fabsf(ctx->smoothed_rtt_ms - rtt_ms) * 0.25f;
        ctx->smoothed_rtt_ms = ctx->smoothed_rtt_ms * 0.875f + rtt_ms * 0.125f;
    }
}

static uint32_t calculate_rto(reliability_context_t* ctx) {
    float rto = ctx->smoothed_rtt_ms + 4 * ctx->rtt_variance_ms;
    if (rto < RELIABILITY_BASE_RTO_MS) rto = RELIABILITY_BASE_RTO_MS;
    if (rto > RELIABILITY_MAX_RTO_MS) rto = RELIABILITY_MAX_RTO_MS;
    return (uint32_t)rto;
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

reliability_context_t* ps4splitscreen_reliability_create(ps4_socket_t* socket, 
                                                          const struct sockaddr_in* remote_addr,
                                                          uint32_t channel_count) {
    if (!socket || socket->type != SOCKET_TYPE_UDP || channel_count > 4) return NULL;

    reliability_context_t* ctx = calloc(1, sizeof(reliability_context_t));
    if (!ctx) return NULL;

    ctx->socket = socket;
    ctx->remote_addr = *remote_addr;
    ctx->channel_count = channel_count;
    ctx->connected = true;
    ctx->last_ack_us = get_time_us();
    ctx->last_keepalive_us = get_time_us();

    for (uint32_t i = 0; i < channel_count; i++) {
        ctx->channels[i].local_sequence = 1;
        ctx->channels[i].remote_sequence = 0;
        ctx->channels[i].ack_sequence = 0;
        ctx->channels[i].ack_bitfield = 0;
    }

    return ctx;
}

void ps4splitscreen_reliability_destroy(reliability_context_t* ctx) {
    if (!ctx) return;
    
    // Flush send buffers
    for (uint32_t c = 0; c < ctx->channel_count; c++) {
        for (int i = 0; i < RELIABILITY_WINDOW_SIZE; i++) {
            if (ctx->channels[c].send_buffer[i].packet) {
                free_packet(ctx->channels[c].send_buffer[i].packet);
            }
            if (ctx->channels[c].recv_buffer[i].packet) {
                free_packet(ctx->channels[c].recv_buffer[i].packet);
            }
        }
    }
    free(ctx);
}

void ps4splitscreen_reliability_set_callback(reliability_context_t* ctx, 
                                              uint8_t channel,
                                              void (*callback)(void*, uint8_t, const uint8_t*, size_t),
                                              void* user_data) {
    if (ctx && channel < ctx->channel_count) {
        ctx->channels[channel].on_data = callback;
        ctx->channels[channel].user_data = user_data;
    }
}

// -----------------------------------------------------------------------------
// Sending
// -----------------------------------------------------------------------------

int ps4splitscreen_reliability_send(reliability_context_t* ctx, 
                                     uint8_t channel, 
                                     const void* data, 
                                     size_t size,
                                     uint8_t flags) {
    if (!ctx || channel >= ctx->channel_count || !data || size > PS4SPLITSCREEN_MAX_PAYLOAD_SIZE) {
        return PS4SPLITSCREEN_ERR_INVALID_PARAM;
    }
    if (!ctx->connected) return PS4SPLITSCREEN_ERR_NOT_CONNECTED;

    reliability_channel_t* ch = &ctx->channels[channel];
    uint16_t seq = ch->local_sequence;
    
    // Find free send buffer slot
    int slot = -1;
    for (int i = 0; i < RELIABILITY_WINDOW_SIZE; i++) {
        int idx = (seq - 1 + i) % RELIABILITY_WINDOW_SIZE;
        if (!ch->send_buffer[idx].pending) {
            slot = idx;
            break;
        }
    }
    if (slot == -1) return PS4SPLITSCREEN_ERR_OUT_OF_MEMORY; // Window full

    // Allocate and build packet
    reliability_packet_t* pkt = alloc_packet(size);
    if (!pkt) return PS4SPLITSCREEN_ERR_OUT_OF_MEMORY;

    pkt->magic = 0x52454C59;
    pkt->sequence = seq;
    pkt->ack_sequence = ch->remote_sequence;
    pkt->ack_bitfield = (uint16_t)(ch->ack_bitfield & 0xFFFF);
    pkt->flags = flags | REL_FLAG_ACK;
    pkt->channel = channel;
    pkt->payload_size = (uint16_t)size;
    pkt->timestamp_us = get_time_us();
    memcpy(pkt->payload, data, size);

    // Store in send buffer
    ch->send_buffer[slot].packet = pkt;
    ch->send_buffer[slot].send_time_us = pkt->timestamp_us;
    ch->send_buffer[slot].retries = 0;
    ch->send_buffer[slot].pending = true;

    // Send immediately
    int sent = ps4_socket_sendto(ctx->socket, pkt, RELIABILITY_HEADER_SIZE + size,
                                  inet_ntoa(ctx->remote_addr.sin_addr),
                                  sceNetNtohs(ctx->remote_addr.sin_port));
    
    if (sent > 0) {
        ch->local_sequence++;
        ch->packets_sent++;
        ch->bytes_sent += size;
        return (int)seq;
    } else {
        free_packet(pkt);
        ch->send_buffer[slot].pending = false;
        return sent;
    }
}

// -----------------------------------------------------------------------------
// Receiving & Processing
// -----------------------------------------------------------------------------

static void process_ack(reliability_context_t* ctx, reliability_channel_t* ch, 
                         uint16_t ack_seq, uint16_t ack_bitfield) {
    // Process explicit ACK
    if (seq_greater(ack_seq, ch->ack_sequence)) {
        uint16_t diff = seq_diff(ack_seq, ch->ack_sequence);
        for (uint16_t i = 0; i < diff; i++) {
            uint16_t seq = (uint16_t)(ch->ack_sequence + 1 + i);
            int idx = (seq - 1) % RELIABILITY_WINDOW_SIZE;
            if (ch->send_buffer[idx].pending) {
                free_packet(ch->send_buffer[idx].packet);
                ch->send_buffer[idx].pending = false;
                ch->send_buffer[idx].packet = NULL;
            }
        }
        ch->ack_sequence = ack_seq;
    }

    // Process SACK bitfield (acknowledges up to 16 prior sequences)
    for (int i = 0; i < 16; i++) {
        if (ack_bitfield & (1 << i)) {
            uint16_t seq = (uint16_t)(ack_seq - 1 - i);
            int idx = (seq - 1) % RELIABILITY_WINDOW_SIZE;
            if (ch->send_buffer[idx].pending) {
                free_packet(ch->send_buffer[idx].packet);
                ch->send_buffer[idx].pending = false;
                ch->send_buffer[idx].packet = NULL;
            }
        }
    }
}

static void process_data(reliability_context_t* ctx, reliability_channel_t* ch, 
                          reliability_packet_t* pkt) {
    uint16_t seq = pkt->sequence;
    uint16_t expected = (uint16_t)(ch->remote_sequence + 1);

    if (seq == expected) {
        // In-order delivery
        ch->remote_sequence = seq;
        
        // Deliver to callback
        if (ch->on_data) {
            ch->on_data(ch->user_data, ch->channel, pkt->payload, pkt->payload_size);
        }
        ch->packets_received++;
        ch->bytes_received += pkt->payload_size;

        // Check for buffered out-of-order packets
        while (true) {
            uint16_t next_seq = (uint16_t)(ch->remote_sequence + 1);
            int idx = (next_seq - 1) % RELIABILITY_WINDOW_SIZE;
            if (ch->recv_buffer[idx].valid) {
                reliability_packet_t* buffered = ch->recv_buffer[idx].packet;
                ch->recv_buffer[idx].valid = false;
                ch->recv_buffer[idx].packet = NULL;
                
                if (buffered->sequence == next_seq) {
                    ch->remote_sequence = next_seq;
                    if (ch->on_data) {
                        ch->on_data(ch->user_data, ch->channel, buffered->payload, buffered->payload_size);
                    }
                    ch->packets_received++;
                    ch->bytes_received += buffered->payload_size;
                    free_packet(buffered);
                } else {
                    // Shouldn't happen
                    free_packet(buffered);
                }
            } else {
                break;
            }
        }
    } else if (seq_greater(seq, expected)) {
        // Out of order - buffer it
        int idx = (seq - 1) % RELIABILITY_WINDOW_SIZE;
        if (!ch->recv_buffer[idx].valid) {
            ch->recv_buffer[idx].packet = pkt;
            ch->recv_buffer[idx].valid = true;
            return; // Don't free, we're keeping it
        }
        // Already have this sequence
    }
    // Else: duplicate or old packet, ignore
    
    free_packet(pkt);
}

int ps4splitscreen_reliability_receive(reliability_context_t* ctx) {
    if (!ctx) return PS4SPLITSCREEN_ERR_INVALID_PARAM;

    reliability_packet_t* pkt = malloc(RELIABILITY_HEADER_SIZE + PS4SPLITSCREEN_MAX_PAYLOAD_SIZE);
    if (!pkt) return PS4SPLITSCREEN_ERR_OUT_OF_MEMORY;

    struct sockaddr_in src;
    int received = ps4_socket_recv(ctx->socket, pkt, RELIABILITY_HEADER_SIZE + PS4SPLITSCREEN_MAX_PAYLOAD_SIZE, &src);
    
    if (received <= 0) {
        free(pkt);
        return received;
    }

    if (received < RELIABILITY_HEADER_SIZE || pkt->magic != 0x52454C59) {
        free(pkt);
        return PS4SPLITSCREEN_OK; // Not our packet
    }

    if (pkt->channel >= ctx->channel_count) {
        free(pkt);
        return PS4SPLITSCREEN_OK;
    }

    reliability_channel_t* ch = &ctx->channels[pkt->channel];

    // Process ACK
    if (pkt->flags & REL_FLAG_ACK) {
        process_ack(ctx, ch, pkt->ack_sequence, pkt->ack_bitfield);
        
        // Update RTT
        uint64_t now = get_time_us();
        int idx = (pkt->ack_sequence - 1) % RELIABILITY_WINDOW_SIZE;
        if (idx >= 0 && idx < RELIABILITY_WINDOW_SIZE && ch->send_buffer[idx].pending) {
            float rtt = (float)(now - ch->send_buffer[idx].send_time_us) / 1000.0f;
            update_rtt(ctx, rtt);
        }
    }

    // Process data
    if (pkt->payload_size > 0) {
        process_data(ctx, ch, pkt);
    } else {
        free_packet(pkt);
    }

    return PS4SPLITSCREEN_OK;
}

// -----------------------------------------------------------------------------
// Update (Retransmission, Keepalive)
// -----------------------------------------------------------------------------

int ps4splitscreen_reliability_update(reliability_context_t* ctx) {
    if (!ctx) return PS4SPLITSCREEN_ERR_INVALID_PARAM;

    uint64_t now = get_time_us();
    uint32_t rto = calculate_rto(ctx);

    // Retransmission check
    for (uint32_t c = 0; c < ctx->channel_count; c++) {
        reliability_channel_t* ch = &ctx->channels[c];
        
        for (int i = 0; i < RELIABILITY_WINDOW_SIZE; i++) {
            if (ch->send_buffer[i].pending) {
                uint64_t elapsed = now - ch->send_buffer[i].send_time_us;
                if (elapsed >= (uint64_t)rto * 1000) {
                    // Retransmit
                    if (ch->send_buffer[i].retries < RELIABILITY_MAX_RETRIES) {
                        reliability_packet_t* pkt = ch->send_buffer[i].packet;
                        pkt->timestamp_us = now;
                        pkt->ack_sequence = ch->remote_sequence;
                        pkt->ack_bitfield = (uint16_t)(ch->ack_bitfield & 0xFFFF);
                        
                        int sent = ps4_socket_sendto(ctx->socket, pkt, 
                            RELIABILITY_HEADER_SIZE + pkt->payload_size,
                            inet_ntoa(ctx->remote_addr.sin_addr),
                            sceNetNtohs(ctx->remote_addr.sin_port));
                        
                        if (sent > 0) {
                            ch->send_buffer[i].send_time_us = now;
                            ch->send_buffer[i].retries++;
                            ch->packets_retransmitted++;
                        }
                    } else {
                        // Max retries exceeded - connection lost
                        ctx->connected = false;
                        return PS4SPLITSCREEN_ERR_TIMEOUT;
                    }
                }
            }
        }
    }

    // Send delayed ACKs
    if ((now - ctx->last_ack_us) >= (uint64_t)RELIABILITY_ACK_DELAY_MS * 1000) {
        for (uint32_t c = 0; c < ctx->channel_count; c++) {
            reliability_channel_t* ch = &ctx->channels[c];
            
            // Build ACK packet (no payload)
            reliability_packet_t ack_pkt = {0};
            ack_pkt.magic = 0x52454C59;
            ack_pkt.sequence = 0; // Pure ACK has no sequence
            ack_pkt.ack_sequence = ch->remote_sequence;
            ack_pkt.ack_bitfield = (uint16_t)(ch->ack_bitfield & 0xFFFF);
            ack_pkt.flags = REL_FLAG_ACK;
            ack_pkt.channel = c;
            ack_pkt.payload_size = 0;
            ack_pkt.timestamp_us = now;

            ps4_socket_sendto(ctx->socket, &ack_pkt, RELIABILITY_HEADER_SIZE,
                              inet_ntoa(ctx->remote_addr.sin_addr),
                              sceNetNtohs(ctx->remote_addr.sin_port));
        }
        ctx->last_ack_us = now;
    }

    // Keepalive
    if ((now - ctx->last_keepalive_us) >= (uint64_t)RELIABILITY_KEEPALIVE_MS * 1000) {
        reliability_packet_t ka_pkt = {0};
        ka_pkt.magic = 0x52454C59;
        ka_pkt.sequence = 0;
        ka_pkt.ack_sequence = 0;
        ka_pkt.flags = REL_FLAG_ACK;
        ka_pkt.channel = 0;
        ka_pkt.payload_size = 0;
        ka_pkt.timestamp_us = now;

        ps4_socket_sendto(ctx->socket, &ka_pkt, RELIABILITY_HEADER_SIZE,
                          inet_ntoa(ctx->remote_addr.sin_addr),
                          sceNetNtohs(ctx->remote_addr.sin_port));
        ctx->last_keepalive_us = now;
    }

    return PS4SPLITSCREEN_OK;
}

bool ps4splitscreen_reliability_is_connected(const reliability_context_t* ctx) {
    return ctx && ctx->connected;
}

float ps4splitscreen_reliability_get_rtt(const reliability_context_t* ctx) {
    return ctx ? ctx->smoothed_rtt_ms : 0.0f;
}

void ps4splitscreen_reliability_get_stats(const reliability_context_t* ctx, 
                                           uint32_t channel,
                                           uint64_t* sent, uint64_t* recv, 
                                           uint64_t* retrans, uint64_t* lost) {
    if (!ctx || channel >= ctx->channel_count) return;
    const reliability_channel_t* ch = &ctx->channels[channel];
    if (sent) *sent = ch->packets_sent;
    if (recv) *recv = ch->packets_received;
    if (retrans) *retrans = ch->packets_retransmitted;
    if (lost) *lost = ch->packets_lost;
}