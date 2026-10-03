#ifndef INDIGO_LOG_H
#define INDIGO_LOG_H

#include <stdbool.h>

void indigo_log_init(void);
void indigo_log_shutdown(void);

/*
 * Also write the log to `path`. The previous file is kept as `<path>.old`.
 * Callers log identifiers and states, never tokens or passwords.
 */
bool indigo_log_open_file(const char *path);

void indigo_log_info(const char *format, ...) __attribute__((format(printf, 1, 2)));
void indigo_log_warn(const char *format, ...) __attribute__((format(printf, 1, 2)));
void indigo_log_error(const char *format, ...) __attribute__((format(printf, 1, 2)));

#endif
