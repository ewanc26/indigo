/* Cache store — timeline/profile on-card cache (indigo #13). Versioned line
 * format, atomic rename, corrupt moved to .bad. Uses indigo_file_write_atomic
 * and indigo_file_read from file.c. No libctru-specific calls in this file; SDMC
 * path is passed by caller (app layer owns the directory). */
#include "store/cache_codec.h"
#include "store/file.h"
#include <string.h>
#include <stdio.h>

indigo_store_status
indigo_cache_write_timeline(const char *path, const char *data, size_t len)
{
    return indigo_file_write_atomic(path, data, len);
}

indigo_store_status
indigo_cache_read_timeline(const char *path, char *buf, size_t cap, size_t *len)
{
    return indigo_file_read(path, buf, cap, len);
}
