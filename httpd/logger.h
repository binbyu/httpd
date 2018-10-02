#ifndef __LOGGER_H__
#define __LOGGER_H__


typedef enum
{
    LOG_DEBU,
    LOG_INFO,
    LOG_WARN,
    LOG_ERRO
} log_level_t;

#define LOG_LEVEL   LOG_INFO
#define SAVE_FILE   1

void log_debug(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_error(const char *fmt, ...);

#endif