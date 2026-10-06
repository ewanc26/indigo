#ifndef INDIGO_UPDATE_SD_H
#define INDIGO_UPDATE_SD_H

#include "update/update.h"

#if defined(__3DS__) && defined(WOLFRAM_3DS)
/* stdio over sdmc:, hashing with Wolfram's SHA-256. */
const indigo_update_fs *indigo_update_sd_fs(void);
#endif

/* Write `len` bytes to `path` through a temporary file and a rename, so the
 * file is either absent or whole. False in builds without the console. */
bool indigo_update_sd_write_file(const char *path, const void *data, size_t len);

/* Finish or undo an interrupted update. Call once at start-up, before
 * romfsInit(), with argv[0]. A no-op when the build was not launched from a
 * .3dsx on the SD card, and in builds without Wolfram. */
void indigo_update_sd_recover(const char *argv0, const char *state_path);

#endif
