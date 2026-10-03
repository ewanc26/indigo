#include "util/log.h"

#include <stdarg.h>
#include <stdio.h>

static void __attribute__((format(printf, 2, 0)))
indigo_log_v(const char *level, const char *format, va_list args)
{
    fprintf(stderr, "[%s] ", level);
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
}

void
indigo_log_init(void)
{
}

void
indigo_log_shutdown(void)
{
}

void
indigo_log_info(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    indigo_log_v("INFO", format, args);
    va_end(args);
}

void
indigo_log_warn(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    indigo_log_v("WARN", format, args);
    va_end(args);
}

void
indigo_log_error(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    indigo_log_v("ERROR", format, args);
    va_end(args);
}
