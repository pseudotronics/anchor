// We explicitly don't use include guards as this file should not be included recursively.

// NOTE: LOGGING_MODULE_NAME can be defined before including this header in order to specify the module
// NOTE: LOGGING_FILE_DEFAULT_LEVEL can be defined before including this header in order to change the default log level

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// The maximum length of a log message (not including extra formatting)
#ifndef LOGGING_MAX_MSG_LENGTH
#define LOGGING_MAX_MSG_LENGTH  128
#endif

// Define this to prefix logs with datetime instead of system time (also changes logging_timestamp_t to a uint64_t)
#ifndef LOGGING_USE_DATETIME
#define LOGGING_USE_DATETIME    0
#endif

// Type which represents a log timestamp in milliseconds
#if LOGGING_USE_DATETIME
typedef uint64_t logging_timestamp_t;
#else
typedef uint32_t logging_timestamp_t;
#endif

typedef enum {
    LOGGING_LEVEL_DEFAULT = 0, // Used to represent the default level specified to logging_init()
    LOGGING_LEVEL_DEBUG,
    LOGGING_LEVEL_INFO,
    LOGGING_LEVEL_WARN,
    LOGGING_LEVEL_ERROR,
} logging_level_t;

typedef struct {
    // Write function which gets passed a fully-formatted log line
    void(*write_function)(const char* str);
    // Write function which gets passed the level and module name split out in addition to the fully-formatted log line
    void(*raw_write_function)(logging_level_t level, const char* module_name, const char* str);
    // A lock function which is called to make the logging library thread-safe
    void(*lock_function)(bool acquire);
    // A function which is called to get the current timestamp in milliseconds (either an epoch time or system uptime)
    logging_timestamp_t(*time_ms_function)(void);
    // The default logging level
    logging_level_t default_level;
} logging_init_t;

// Initialize the logging library
bool logging_init(const logging_init_t* init);

// Logs a line which was manually captured through a printf-style function hook / macro (filtered by the default level)
void logging_log_line(logging_level_t level, const char* file, int line, const char* module_prefix, const char* fmt, va_list args);

// Need to include this after our types or defined
#include "logging_internal.h"

// Change the logging threshold for the current module
#define LOG_SET_LEVEL(LEVEL) _logging_logger.level = LEVEL

// Macros for logging at each level
#define LOG_DEBUG(...) _LOG_LEVEL_IMPL(LOGGING_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_INFO(...) _LOG_LEVEL_IMPL(LOGGING_LEVEL_INFO, __VA_ARGS__)
#define LOG_WARN(...) _LOG_LEVEL_IMPL(LOGGING_LEVEL_WARN, __VA_ARGS__)
#define LOG_ERROR(...) _LOG_LEVEL_IMPL(LOGGING_LEVEL_ERROR, __VA_ARGS__)

#ifdef __cplusplus
}
#endif
