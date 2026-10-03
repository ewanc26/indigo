#ifndef INDIGO_LOG_H
#define INDIGO_LOG_H

void indigo_log_init(void);
void indigo_log_shutdown(void);
void indigo_log_info(const char *format, ...) __attribute__((format(printf, 1, 2)));
void indigo_log_warn(const char *format, ...) __attribute__((format(printf, 1, 2)));
void indigo_log_error(const char *format, ...) __attribute__((format(printf, 1, 2)));

#endif
