#ifndef INDIGO_STORE_CACHE_STORE_H
#define INDIGO_STORE_CACHE_STORE_H

#include "store/cache_codec.h"
#include "store/store_status.h"

/* Write the timeline page to `path`, atomically. */
indigo_store_status indigo_cache_store_save(const char *path, const indigo_cache_timeline *t);

/* Load the page from `path`. On anything but a valid file `out` is empty and
 * the caller shows nothing from the cache: INDIGO_STORE_MISSING when there is
 * no file, INDIGO_STORE_UNREADABLE when the file was damaged and set aside. */
indigo_store_status indigo_cache_store_load(const char *path, indigo_cache_timeline *out);

#endif
