#include "anchor/logging/logging.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define LEVEL_PREFIX_LENGTH     6
#if LOGGING_USE_DATETIME
#define TIME_LENGTH             24
#else
#define TIME_LENGTH             14
#endif
#define FILE_NAME_LENGTH        32
#define FULL_LOG_MAX_LENGTH     (LOGGING_MAX_MSG_LENGTH + LEVEL_PREFIX_LENGTH + TIME_LENGTH + FILE_NAME_LENGTH + 1)

#define BUFFER_PRINTF(BUFFER, SIZE, FMT, ...) ({ \
        const size_t _len = strlen(BUFFER); \
        snprintf(&BUFFER[_len], (SIZE)-_len, FMT, __VA_ARGS__); \
    })

#define BUFFER_VA_PRINTF(BUFFER, SIZE, FMT, ARGS) ({ \
        const size_t _len = strlen(BUFFER); \
        vsnprintf(&BUFFER[_len], (SIZE)-_len, FMT, ARGS); \
    })

#define BUFFER_WRITE(BUFFER, SIZE, STR) BUFFER_PRINTF(BUFFER, SIZE, "%s", STR)

typedef struct {
    logging_level_t level;
    const char* file;
    int line;
    const char* module_prefix;
    logging_timestamp_t timestamp;
    const char* fmt;
    va_list args;
} logging_line_t;

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint16_t ms;
} log_time_t;

#if LOGGING_USE_DATETIME
typedef struct {
    struct {
        uint16_t year;
        uint8_t month;
        uint8_t day;
    } date;
    log_time_t time;
} log_datetime_t;
#endif

static const char* const LEVEL_PREFIX[] = {
    [LOGGING_LEVEL_DEFAULT] =   "????? ", // should never be printed
    [LOGGING_LEVEL_DEBUG] =     "DEBUG ",
    [LOGGING_LEVEL_INFO] =      "INFO  ",
    [LOGGING_LEVEL_WARN] =      "WARN  ",
    [LOGGING_LEVEL_ERROR] =     "ERROR ",
};
#if LOGGING_USE_DATETIME
static const uint8_t DAYS_PER_MONTH[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
#endif

static logging_init_t m_init;

static void timestamp_to_time(logging_timestamp_t timestamp, log_time_t* time) {
    time->ms = timestamp % 1000;
    timestamp /= 1000;
    time->second = timestamp % 60;
    timestamp /= 60;
    time->minute = timestamp % 60;
    timestamp /= 60;
    time->hour = timestamp;
}

#if LOGGING_USE_DATETIME
static bool is_leap_year(uint16_t year) {
    // A year is a leap year if it's evenly divisible by 4 unless it's evenly divisible by 100 but not 400.
    return ((year & 3) == 0 && (year % 100)) || (year % 400) == 0;
}

static void timestamp_to_datetime(logging_timestamp_t ms, log_datetime_t* datetime) {
    // Calculate the time portion
    timestamp_to_time(ms % 86400000, &datetime->time);

    // Calculate days since epoch (add 1 for the current day)
    uint32_t days = ms / 86400000 + 1;

    // Calculate the year
    uint16_t days_in_year = 365;
    datetime->date.year = 1970;
    while (days > days_in_year) {
        days -= days_in_year;
        datetime->date.year++;
        if (is_leap_year(datetime->date.year)) {
            // this is a leap year
            days_in_year = 366;
        } else {
            days_in_year = 365;
        }
    }

    // Calculate the month and day
    datetime->date.month = 0;
    for (uint8_t month = 1; month <= 12; month++) {
        const uint8_t days_in_month = (month == 2 && is_leap_year(datetime->date.year)) ? 29 : DAYS_PER_MONTH[month-1];
        if (days <= days_in_month) {
            datetime->date.month = month;
            break;
        } else {
            days -= days_in_month;
        }
    }
    datetime->date.day = days;
}
#endif

static void format_log_line(const logging_line_t* log_line, char* buffer, size_t size) {
    buffer[0] = '\0';

    // time
    if (log_line->timestamp) {
#if LOGGING_USE_DATETIME
        log_datetime_t datetime;
        timestamp_to_datetime(log_line->timestamp, &datetime);
        BUFFER_PRINTF(buffer, size, "%04u-%02u-%02u %02u:%02u:%02u.%03u ", datetime.date.year, datetime.date.month,
            datetime.date.day, datetime.time.hour, datetime.time.minute, datetime.time.second, datetime.time.ms);
#else
        log_time_t time;
        timestamp_to_time(log_line->timestamp, &time);
        BUFFER_PRINTF(buffer, size, "%3u:%02u:%02u.%03u ", time.hour, time.minute, time.second, time.ms);
#endif
    }

    // Add the level
    BUFFER_WRITE(buffer, size, LEVEL_PREFIX[log_line->level]);

    // Add the module (if set)
    if (log_line->module_prefix) {
        BUFFER_WRITE(buffer, size, log_line->module_prefix);
    }

    // Add the file name
    BUFFER_WRITE(buffer, size, log_line->file);

    // Add the line number
    BUFFER_PRINTF(buffer, size, ":%d: ", log_line->line);

    // Add the formatted log message
    BUFFER_VA_PRINTF(buffer, size, log_line->fmt, log_line->args);

    // Always add a newline, even if it means truncating the message
    if (strlen(buffer) == size - 1) {
        // Need to make space for the newline
        buffer[size-2] = '\0';
    }
    BUFFER_WRITE(buffer, size, "\n");
}

static void handle_log_line(const logging_line_t* log_line) {
    static char buffer[FULL_LOG_MAX_LENGTH];
    format_log_line(log_line, buffer, sizeof(buffer));
    if (m_init.write_function) {
        m_init.write_function(buffer);
    }
    if (m_init.raw_write_function) {
        m_init.raw_write_function(log_line->level, log_line->module_prefix, buffer);
    }
}

static void log_line_helper(logging_level_t level, const char* file, int line, const char* module_prefix, const char* fmt, va_list args) {
    if (m_init.lock_function) {
        m_init.lock_function(true);
    }
    const logging_line_t log_line = {
        .level = level,
        .file = file,
        .line = line,
        .module_prefix = module_prefix,
        .timestamp = m_init.time_ms_function ? m_init.time_ms_function() : 0,
        .fmt = fmt,
        .args = args,
    };
    handle_log_line(&log_line);
    if (m_init.lock_function) {
        m_init.lock_function(false);
    }
}

bool logging_init(const logging_init_t* init) {
    if ((!init->write_function && !init->raw_write_function) || init->default_level == LOGGING_LEVEL_DEFAULT) {
        return false;
    }
    m_init = *init;
    return true;
}

void logging_log_line(logging_level_t level, const char* file, int line, const char* module_prefix, const char* fmt, va_list args) {
    if (level < m_init.default_level) {
        return;
    }
    log_line_helper(level, file, line, module_prefix, fmt, args);
}

void logging_log_impl(logging_logger_t* logger, logging_level_t level, const char* file, int line, const char* fmt, ...) {
    const logging_level_t min_level = logger->level == LOGGING_LEVEL_DEFAULT ? m_init.default_level : logger->level;
    if (level < min_level) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    log_line_helper(level, file, line, logger->module_prefix, fmt, args);
    va_end(args);
}
