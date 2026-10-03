#include "util/log.h"

#include <stdarg.h>
#include <stdio.h>

#ifdef __3DS__
#include <3ds.h>
static LightLock s_lock;
#define LOCK() LightLock_Lock(&s_lock)
#define UNLOCK() LightLock_Unlock(&s_lock)
#else
#define LOCK() ((void) 0)
#define UNLOCK() ((void) 0)
#endif

static FILE *s_file;

static void __attribute__((format(printf, 2, 0)))
indigo_log_v(const char *level, const char *format, va_list args)
{
    va_list copy;

    va_copy(copy, args);
    LOCK();
    fprintf(stderr, "[%s] ", level);
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    if (s_file) {
        fprintf(s_file, "[%s] ", level);
        vfprintf(s_file, format, copy);
        fputc('\n', s_file);
        fflush(s_file);
    }
    UNLOCK();
    va_end(copy);
}

void
indigo_log_init(void)
{
#ifdef __3DS__
    LightLock_Init(&s_lock);
#endif
}

void
indigo_log_shutdown(void)
{
    LOCK();
    if (s_file) {
        fclose(s_file);
        s_file = NULL;
    }
    UNLOCK();
}

bool
indigo_log_open_file(const char *path)
{
    char old[300];
    FILE *f;

    if (snprintf(old, sizeof old, "%s.old", path) >= (int) sizeof old) {
        return false;
    }
    remove(old);
    rename(path, old);
    f = fopen(path, "w");
    if (!f) {
        return false;
    }
    LOCK();
    s_file = f;
    UNLOCK();
    return true;
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
