/**
 * @file logger.c
 * @brief Logging System for PS4 Homebrew
 * 
 * Supports multiple output targets: debug console, file, network.
 */

#include <ps4splitscreen/ps4splitscreen.h>
#include <ps4/debug.h>
#include <ps4/stdio.h>
#include <ps4/kernel.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// -----------------------------------------------------------------------------
// Log Levels
// -----------------------------------------------------------------------------
#define LOG_LEVEL_NONE  0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_INFO  3
#define LOG_LEVEL_DEBUG 4

static int g_log_level = LOG_LEVEL_INFO;
static ps4splitscreen_log_fn g_log_callback = NULL;
static void* g_log_user_data = NULL;
static FILE* g_log_file = NULL;
static bool g_log_to_debug = true;
static char g_log_buffer[4096];

// -----------------------------------------------------------------------------
// Internal
// -----------------------------------------------------------------------------
static const char* level_to_string(int level) {
    switch (level) {
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_WARN:  return "WARN ";
        case LOG_LEVEL_INFO:  return "INFO ";
        case LOG_LEVEL_DEBUG: return "DEBUG";
        default: return "UNKWN";
    }
}

static void format_timestamp(char* buf, size_t size) {
    time_t t = time(NULL);
    struct tm* tm = localtime(&t);
    snprintf(buf, size, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
}

static void output_log(int level, const char* tag, const char* msg) {
    char timestamp[16];
    format_timestamp(timestamp, sizeof(timestamp));
    
    // Format final message
    int len = snprintf(g_log_buffer, sizeof(g_log_buffer), 
                       "[%s] [%s] [%s] %s\n", timestamp, level_to_string(level), tag, msg);
    
    // Debug console (sceDbgPrintf)
    if (g_log_to_debug) {
        sceDbgPrintf("%s", g_log_buffer);
    }
    
    // File
    if (g_log_file) {
        fwrite(g_log_buffer, 1, len, g_log_file);
        fflush(g_log_file);
    }
    
    // Callback
    if (g_log_callback) {
        g_log_callback(level, tag, "%s", g_log_buffer);
    }
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

void ps4splitscreen_logger_init(int level, bool to_debug, const char* file_path) {
    g_log_level = level;
    g_log_to_debug = to_debug;
    
    if (file_path) {
        g_log_file = fopen(file_path, "a");
        if (g_log_file) {
            setvbuf(g_log_file, NULL, _IOLBF, 0); // Line buffered
        }
    }
}

void ps4splitscreen_logger_set_callback(ps4splitscreen_log_fn callback, void* user_data) {
    g_log_callback = callback;
    g_log_user_data = user_data;
}

void ps4splitscreen_logger_set_level(int level) {
    g_log_level = level;
}

void ps4splitscreen_logger_shutdown(void) {
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    g_log_callback = NULL;
}

void ps4splitscreen_log(int level, const char* tag, const char* fmt, ...) {
    if (level > g_log_level || level <= LOG_LEVEL_NONE) return;
    if (!tag || !fmt) return;
    
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(g_log_buffer, sizeof(g_log_buffer), fmt, args);
    va_end(args);
    
    if (len > 0) {
        output_log(level, tag, g_log_buffer);
    }
}

void ps4splitscreen_logv(int level, const char* tag, const char* fmt, va_list args) {
    if (level > g_log_level || level <= LOG_LEVEL_NONE) return;
    if (!tag || !fmt) return;
    
    int len = vsnprintf(g_log_buffer, sizeof(g_log_buffer), fmt, args);
    if (len > 0) {
        output_log(level, tag, g_log_buffer);
    }
}

// Convenience macros
#define LOG_ERROR(tag, ...) ps4splitscreen_log(LOG_LEVEL_ERROR, tag, __VA_ARGS__)
#define LOG_WARN(tag, ...)  ps4splitscreen_log(LOG_LEVEL_WARN, tag, __VA_ARGS__)
#define LOG_INFO(tag, ...)  ps4splitscreen_log(LOG_LEVEL_INFO, tag, __VA_ARGS__)
#define LOG_DEBUG(tag, ...) ps4splitscreen_log(LOG_LEVEL_DEBUG, tag, __VA_ARGS__)

// -----------------------------------------------------------------------------
// Hex Dump Utility
// -----------------------------------------------------------------------------
void ps4splitscreen_log_hexdump(int level, const char* tag, const void* data, size_t size) {
    if (level > g_log_level) return;
    if (!data || size == 0) return;
    
    const uint8_t* bytes = (const uint8_t*)data;
    char line[128];
    
    for (size_t offset = 0; offset < size; offset += 16) {
        size_t line_len = (size - offset < 16) ? size - offset : 16;
        int pos = 0;
        
        pos += snprintf(line + pos, sizeof(line) - pos, "%08zX: ", offset);
        
        // Hex
        for (size_t i = 0; i < 16; i++) {
            if (i == 8) pos += snprintf(line + pos, sizeof(line) - pos, " ");
            if (offset + i < size) {
                pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", bytes[offset + i]);
            } else {
                pos += snprintf(line + pos, sizeof(line) - pos, "   ");
            }
        }
        
        pos += snprintf(line + pos, sizeof(line) - pos, " |");
        
        // ASCII
        for (size_t i = 0; i < line_len; i++) {
            uint8_t c = bytes[offset + i];
            pos += snprintf(line + pos, sizeof(line) - pos, "%c", (c >= 32 && c < 127) ? c : '.');
        }
        
        pos += snprintf(line + pos, sizeof(line) - pos, "|");
        
        output_log(level, tag, line);
    }
}

// -----------------------------------------------------------------------------
// Scoped Logger (for function entry/exit)
// -----------------------------------------------------------------------------
typedef struct {
    const char* tag;
    const char* function;
    uint64_t start_time;
} scoped_logger_t;

scoped_logger_t* ps4splitscreen_log_scope_begin(const char* tag, const char* function) {
    scoped_logger_t* scope = malloc(sizeof(scoped_logger_t));
    if (!scope) return NULL;
    
    scope->tag = tag;
    scope->function = function;
    scope->start_time = get_time_us();
    
    LOG_DEBUG(tag, "ENTER: %s", function);
    return scope;
}

void ps4splitscreen_log_scope_end(scoped_logger_t* scope) {
    if (!scope) return;
    
    uint64_t elapsed = get_time_us() - scope->start_time;
    LOG_DEBUG(scope->tag, "EXIT: %s (%.3f ms)", scope->function, elapsed / 1000.0f);
    free(scope);
}

// -----------------------------------------------------------------------------
// Performance Markers
// -----------------------------------------------------------------------------
typedef struct {
    const char* name;
    uint64_t start_time;
    uint32_t count;
    uint64_t total_time;
    uint64_t min_time;
    uint64_t max_time;
} perf_marker_t;

#define MAX_PERF_MARKERS 32
static perf_marker_t g_perf_markers[MAX_PERF_MARKERS];
static uint32_t g_perf_marker_count = 0;

perf_marker_t* ps4splitscreen_perf_begin(const char* name) {
    if (g_perf_marker_count >= MAX_PERF_MARKERS) return NULL;
    
    perf_marker_t* m = &g_perf_markers[g_perf_marker_count++];
    m->name = name;
    m->start_time = get_time_us();
    m->count = 0;
    m->total_time = 0;
    m->min_time = UINT64_MAX;
    m->max_time = 0;
    
    return m;
}

void ps4splitscreen_perf_end(perf_marker_t* marker) {
    if (!marker) return;
    
    uint64_t elapsed = get_time_us() - marker->start_time;
    marker->count++;
    marker->total_time += elapsed;
    if (elapsed < marker->min_time) marker->min_time = elapsed;
    if (elapsed > marker->max_time) marker->max_time = elapsed;
}

void ps4splitscreen_perf_report(void) {
    LOG_INFO("PERF", "=== Performance Report ===");
    for (uint32_t i = 0; i < g_perf_marker_count; i++) {
        perf_marker_t* m = &g_perf_markers[i];
        if (m->count > 0) {
            float avg = (float)m->total_time / m->count / 1000.0f;
            float min = (float)m->min_time / 1000.0f;
            float max = (float)m->max_time / 1000.0f;
            LOG_INFO("PERF", "%s: count=%u avg=%.3fms min=%.3fms max=%.3fms", 
                     m->name, m->count, avg, min, max);
        }
    }
    g_perf_marker_count = 0;
}

// Helper for time
static uint64_t get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}