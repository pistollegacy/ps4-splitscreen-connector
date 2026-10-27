/**
 * @file packet_serialization.c
 * @brief Packet Serialization for Game State & Commands
 * 
 * Binary serialization with versioning and CRC for network packets.
 */

#include <ps4splitscreen/ps4splitscreen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Packet Types
// -----------------------------------------------------------------------------
typedef enum {
    PKT_NONE = 0,
    
    // Connection
    PKT_HELLO = 0x10,
    PKT_WELCOME = 0x11,
    PKT_GOODBYE = 0x12,
    
    // Synchronization
    PKT_INPUT = 0x20,
    PKT_INPUT_ACK = 0x21,
    PKT_STATE = 0x22,
    PKT_STATE_REQUEST = 0x23,
    PKT_STATE_DELTA = 0x24,
    PKT_CHECKPOINT = 0x25,
    PKT_ROLLBACK = 0x26,
    
    // Session
    PKT_PLAYER_JOIN = 0x30,
    PKT_PLAYER_LEAVE = 0x31,
    PKT_PAUSE = 0x32,
    PKT_RESUME = 0x33,
    
    // Video/Render (for future streaming)
    PKT_FRAME_REQUEST = 0x40,
    PKT_FRAME_DATA = 0x41,
} packet_type_t;

// -----------------------------------------------------------------------------
// Packet Header
// -----------------------------------------------------------------------------
#define PACKET_MAGIC 0x50534E50  // 'PSNP'
#define PACKET_VERSION 1
#define MAX_PACKET_PAYLOAD 1300

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t type;
    uint16_t sequence;
    uint32_t frame_number;
    uint64_t timestamp_us;
    uint16_t payload_size;
    uint16_t crc16;
    uint8_t payload[];
} packet_header_t;

#define PACKET_HEADER_SIZE (sizeof(packet_header_t) - sizeof(((packet_header_t*)0)->payload))

// -----------------------------------------------------------------------------
// CRC16 (CCITT-FALSE)
// -----------------------------------------------------------------------------
static const uint16_t crc16_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE92E, 0xF90F, 0x89E8, 0x99C9, 0xA9AA, 0xB98B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4C44, 0x5C65, 0x2C82, 0x3CA3, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCD4C, 0xDD6D, 0xAD8A, 0xBDAB, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5E75, 0x4E54, 0x3EB3, 0x2E92, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDF7D, 0xCFFC, 0xBF3B, 0xAF1A, 0x9FDD, 0x8FFC,
};

static uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc << 8) ^ crc16_table[((crc >> 8) ^ data[i]) & 0xFF];
    }
    return crc;
}

// -----------------------------------------------------------------------------
// Serialization Helpers
// -----------------------------------------------------------------------------
static void write_u8(uint8_t** ptr, uint8_t val) {
    **ptr = val; (*ptr)++;
}
static void write_u16(uint8_t** ptr, uint16_t val) {
    *((uint16_t*)*ptr) = val; *ptr += 2;
}
static void write_u32(uint8_t** ptr, uint32_t val) {
    *((uint32_t*)*ptr) = val; *ptr += 4;
}
static void write_u64(uint8_t** ptr, uint64_t val) {
    *((uint64_t*)*ptr) = val; *ptr += 8;
}
static void write_bytes(uint8_t** ptr, const void* data, size_t len) {
    memcpy(*ptr, data, len); *ptr += len;
}

static uint8_t read_u8(const uint8_t** ptr) { uint8_t v = **ptr; (*ptr)++; return v; }
static uint16_t read_u16(const uint8_t** ptr) { uint16_t v = *((uint16_t*)*ptr); *ptr += 2; return v; }
static uint32_t read_u32(const uint8_t** ptr) { uint32_t v = *((uint32_t*)*ptr); *ptr += 4; return v; }
static uint64_t read_u64(const uint8_t** ptr) { uint64_t v = *((uint64_t*)*ptr); *ptr += 8; return v; }
static void read_bytes(const uint8_t** ptr, void* dst, size_t len) { memcpy(dst, *ptr, len); *ptr += len; }

// -----------------------------------------------------------------------------
// Input Packet Serialization
// -----------------------------------------------------------------------------

size_t ps4splitscreen_serialize_input(const ps4splitscreen_input_t* input, 
                                       uint8_t* buffer, size_t buffer_size) {
    if (!input || !buffer || buffer_size < 32) return 0;
    
    uint8_t* ptr = buffer;
    write_u32(&ptr, input->frame_number);
    write_u32(&ptr, input->player_id);
    write_u32(&ptr, input->buttons_down);
    write_u32(&ptr, input->buttons_pressed);
    write_u32(&ptr, input->buttons_released);
    write_u16(&ptr, (uint16_t)(input->left_stick_x + 32768));
    write_u16(&ptr, (uint16_t)(input->left_stick_y + 32768));
    write_u16(&ptr, (uint16_t)(input->right_stick_x + 32768));
    write_u16(&ptr, (uint16_t)(input->right_stick_y + 32768));
    write_u16(&ptr, input->left_trigger);
    write_u16(&ptr, input->right_trigger);
    write_u64(&ptr, input->timestamp_us);
    
    return ptr - buffer;
}

bool ps4splitscreen_deserialize_input(const uint8_t* buffer, size_t size, 
                                       ps4splitscreen_input_t* out_input) {
    if (!buffer || !out_input || size < 32) return false;
    
    const uint8_t* ptr = buffer;
    out_input->frame_number = read_u32(&ptr);
    out_input->player_id = read_u32(&ptr);
    out_input->buttons_down = read_u32(&ptr);
    out_input->buttons_pressed = read_u32(&ptr);
    out_input->buttons_released = read_u32(&ptr);
    out_input->left_stick_x = (int16_t)read_u16(&ptr) - 32768;
    out_input->left_stick_y = (int16_t)read_u16(&ptr) - 32768;
    out_input->right_stick_x = (int16_t)read_u16(&ptr) - 32768;
    out_input->right_stick_y = (int16_t)read_u16(&ptr) - 32768;
    out_input->left_trigger = read_u16(&ptr);
    out_input->right_trigger = read_u16(&ptr);
    out_input->timestamp_us = read_u64(&ptr);
    
    return true;
}

// -----------------------------------------------------------------------------
// Full Packet Build/Parse
// -----------------------------------------------------------------------------

uint8_t* ps4splitscreen_packet_create(packet_type_t type, uint16_t sequence, 
                                       uint32_t frame, const void* payload, 
                                       uint16_t payload_size, size_t* out_size) {
    if (payload_size > MAX_PACKET_PAYLOAD) return NULL;
    
    size_t total_size = PACKET_HEADER_SIZE + payload_size;
    uint8_t* packet = malloc(total_size);
    if (!packet) return NULL;
    
    packet_header_t* hdr = (packet_header_t*)packet;
    hdr->magic = PACKET_MAGIC;
    hdr->version = PACKET_VERSION;
    hdr->type = type;
    hdr->sequence = sequence;
    hdr->frame_number = frame;
    
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    hdr->timestamp_us = (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    
    hdr->payload_size = payload_size;
    if (payload && payload_size > 0) {
        memcpy(hdr->payload, payload, payload_size);
    }
    
    // CRC over header (excluding CRC field) + payload
    hdr->crc16 = crc16_ccitt(packet, total_size - 2);
    
    if (out_size) *out_size = total_size;
    return packet;
}

bool ps4splitscreen_packet_verify(const uint8_t* packet, size_t size) {
    if (!packet || size < PACKET_HEADER_SIZE) return false;
    
    const packet_header_t* hdr = (const packet_header_t*)packet;
    if (hdr->magic != PACKET_MAGIC) return false;
    if (hdr->version != PACKET_VERSION) return false;
    if (hdr->payload_size + PACKET_HEADER_SIZE != size) return false;
    
    uint16_t stored_crc = hdr->crc16;
    // Temporarily zero CRC for calculation
    ((packet_header_t*)packet)->crc16 = 0;
    uint16_t calc_crc = crc16_ccitt(packet, size);
    ((packet_header_t*)packet)->crc16 = stored_crc;
    
    return stored_crc == calc_crc;
}

packet_type_t ps4splitscreen_packet_get_type(const uint8_t* packet, size_t size) {
    if (!packet || size < PACKET_HEADER_SIZE) return PKT_NONE;
    const packet_header_t* hdr = (const packet_header_t*)packet;
    if (hdr->magic != PACKET_MAGIC) return PKT_NONE;
    return hdr->type;
}

uint16_t ps4splitscreen_packet_get_sequence(const uint8_t* packet, size_t size) {
    if (!packet || size < PACKET_HEADER_SIZE) return 0;
    const packet_header_t* hdr = (const packet_header_t*)packet;
    if (hdr->magic != PACKET_MAGIC) return 0;
    return hdr->sequence;
}

uint32_t ps4splitscreen_packet_get_frame(const uint8_t* packet, size_t size) {
    if (!packet || size < PACKET_HEADER_SIZE) return 0;
    const packet_header_t* hdr = (const packet_header_t*)packet;
    if (hdr->magic != PACKET_MAGIC) return 0;
    return hdr->frame_number;
}

const uint8_t* ps4splitscreen_packet_get_payload(const uint8_t* packet, size_t size, uint16_t* out_payload_size) {
    if (!packet || size < PACKET_HEADER_SIZE) return NULL;
    const packet_header_t* hdr = (const packet_header_t*)packet;
    if (hdr->magic != PACKET_MAGIC) return NULL;
    if (out_payload_size) *out_payload_size = hdr->payload_size;
    return hdr->payload;
}

void ps4splitscreen_packet_free(uint8_t* packet) {
    free(packet);
}

// -----------------------------------------------------------------------------
// Hello/Welcome Packets (Session Setup)
// -----------------------------------------------------------------------------

typedef struct __attribute__((packed)) {
    uint32_t protocol_version;
    uint32_t game_version;
    uint8_t player_id;
    uint8_t max_players;
    uint8_t sync_mode;
    uint8_t reserved[3];
    char session_token[32];
} hello_payload_t;

typedef struct __attribute__((packed)) {
    uint32_t protocol_version;
    uint32_t game_version;
    uint8_t assigned_player_id;
    uint8_t max_players;
    uint8_t sync_mode;
    uint8_t host_player_id;
    uint32_t initial_frame;
    uint64_t host_time_us;
    char session_token[32];
} welcome_payload_t;

uint8_t* ps4splitscreen_build_hello(uint16_t seq, uint32_t frame, 
                                     uint32_t game_version, uint8_t max_players,
                                     uint8_t sync_mode, const char* token, 
                                     size_t* out_size) {
    hello_payload_t payload = {0};
    payload.protocol_version = PS4SPLITSCREEN_PROTOCOL_VERSION;
    payload.game_version = game_version;
    payload.player_id = 0xFF; // Unassigned
    payload.max_players = max_players;
    payload.sync_mode = sync_mode;
    if (token) strncpy(payload.session_token, token, 31);
    
    return ps4splitscreen_packet_create(PKT_HELLO, seq, frame, &payload, sizeof(payload), out_size);
}

uint8_t* ps4splitscreen_build_welcome(uint16_t seq, uint32_t frame,
                                       uint32_t game_version, uint8_t assigned_id,
                                       uint8_t host_id, uint32_t initial_frame,
                                       const char* token, size_t* out_size) {
    welcome_payload_t payload = {0};
    payload.protocol_version = PS4SPLITSCREEN_PROTOCOL_VERSION;
    payload.game_version = game_version;
    payload.assigned_player_id = assigned_id;
    payload.host_player_id = host_id;
    payload.initial_frame = initial_frame;
    
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    payload.host_time_us = (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    
    if (token) strncpy(payload.session_token, token, 31);
    
    return ps4splitscreen_packet_create(PKT_WELCOME, seq, frame, &payload, sizeof(payload), out_size);
}

bool ps4splitscreen_parse_hello(const uint8_t* packet, size_t size, hello_payload_t* out) {
    if (!packet || !out) return false;
    uint16_t payload_size;
    const uint8_t* payload = ps4splitscreen_packet_get_payload(packet, size, &payload_size);
    if (!payload || payload_size != sizeof(hello_payload_t)) return false;
    memcpy(out, payload, sizeof(hello_payload_t));
    return true;
}

bool ps4splitscreen_parse_welcome(const uint8_t* packet, size_t size, welcome_payload_t* out) {
    if (!packet || !out) return false;
    uint16_t payload_size;
    const uint8_t* payload = ps4splitscreen_packet_get_payload(packet, size, &payload_size);
    if (!payload || payload_size != sizeof(welcome_payload_t)) return false;
    memcpy(out, payload, sizeof(welcome_payload_t));
    return true;
}