/**
 * @file logger.h
 * @brief Logging System
 */

#ifndef PS4SPLITSCREEN_LOGGER_H
#define PS4SPLITSCREEN_LOGGER_H

#include <ps4splitscreen/ps4splitscreen.h>

#ifdef __cplusplus
extern "C" {
#endif

// Log levels
#define PS4SPLITSCREEN_LOG_NONE  0
#define PS4SPLITSCREEN_LOG_ERROR 1
#define PS4SPLITSCREEN_LOG_WARN  2
#define PS4SPLITSCREEN_LOG_INFO  3
#define PS4SPLITSCREEN_LOG_DEBUG 4

void ps4splitscreen_logger_init(int level, bool to_debug, const char* file_path);
void ps4splitscreen_logger_set_callback(ps4splitscreen_log_fn callback, void* user_data);
void ps4splitscreen_logger_set_level(int level);
void ps4splitscreen_logger_shutdown(void);

void ps4splitscreen_log(int level, const char* tag, const char* fmt, ...);
void ps4splitscreen_logv(int level, const char* tag, const char* fmt, va_list args);
void ps4splitscreen_log_hexdump(int level, const char* tag, const void* data, size_t size);

// Scoped logging
typedef struct scoped_logger scoped_logger_t;
scoped_logger_t* ps4splitscreen_log_scope_begin(const char* tag, const char* function);
void ps4splitscreen_log_scope_end(scoped_logger_t* scope);

// Performance markers
typedef struct perf_marker perf_marker_t;
perf_marker_t* ps4splitscreen_perf_begin(const char* name);
void ps4splitscreen_perf_end(perf_marker_t* marker);
void ps4splitscreen_perf_report(void);

#ifdef __cplusplus
}
#endif

#endif // PS4SPLITSCREEN_LOGGER_H