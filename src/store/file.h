#ifndef INDIGO_STORE_FILE_H
#define INDIGO_STORE_FILE_H

/* The file operations every store repeats: write a whole file so a crash never
 * leaves half of it, read a whole file within a cap, set a damaged one aside,
 * and delete one. A store (session, settings, draft) is a codec plus these. */

#include "store/store_status.h"

#include <stddef.h>

/* Write `len` bytes of `data` to `path` by way of `path.tmp` and a rename, so
 * the old file stays whole until the new one is complete. FAT will not rename
 * over an existing file on every libc, so the target is removed first. */
indigo_store_status indigo_file_write_atomic(const char *path, const void *data, size_t len);

/* Read `path` into `buf`, at most `cap` bytes, storing the count in `*len`.
 * INDIGO_STORE_MISSING when the file does not exist, INDIGO_STORE_IO when it
 * cannot be read. A file longer than `cap` is read as `cap` bytes, which the
 * caller's decoder sees as too big: size `buf` one byte past the largest valid
 * file. */
indigo_store_status indigo_file_read(const char *path, void *buf, size_t cap, size_t *len);

/* Move a file that would not decode to `path.bad`, replacing an earlier one,
 * rather than deleting what may be someone's data. */
void indigo_file_set_aside(const char *path);

/* Delete `path`. A file that is already gone is fine; one that is still there
 * afterwards is INDIGO_STORE_IO. */
indigo_store_status indigo_file_remove(const char *path);

#endif
