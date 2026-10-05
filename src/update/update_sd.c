/* The SD-card side of self-update: the indigo_update_fs the swap runs
 * against, and the start-up recovery. Only the 3DS build with Wolfram compiles
 * the body; the decisions are in update.c and tested on the host. */
#include "update/update_sd.h"

#include "util/buildinfo.h"
#include "util/log.h"

#if defined(__3DS__) && defined(WOLFRAM_3DS)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wolfram/crypto.h>

static bool
sd_exists(void *ctx, const char *path)
{
    (void) ctx;
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    fclose(f);
    return true;
}

static bool
sd_rename(void *ctx, const char *from, const char *to)
{
    (void) ctx;
    return rename(from, to) == 0;
}

static bool
sd_remove(void *ctx, const char *path)
{
    (void) ctx;
    return remove(path) == 0;
}

/* Read a whole file, up to the update ceiling. */
static unsigned char *
slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long n;

    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) <= 0 ||
        (unsigned long) n > INDIGO_UPDATE_MAX_BYTES || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = malloc((size_t) n);
    if (!buf || fread(buf, 1, (size_t) n, f) != (size_t) n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (size_t) n;
    return buf;
}

/* Write a temporary and rename it, so a copy is all or nothing: the swap's
 * recovery relies on never seeing half a .3dsx at the target. */
static bool
write_whole(const char *path, const void *data, size_t len)
{
    char tmp[INDIGO_UPDATE_PATH_MAX + 8];
    FILE *f;
    bool ok;

    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int) sizeof tmp) {
        return false;
    }
    f = fopen(tmp, "wb");
    if (!f) {
        return false;
    }
    ok = fwrite(data, 1, len, f) == len;
    ok = fclose(f) == 0 && ok;
    if (!ok) {
        remove(tmp);
        return false;
    }
    remove(path);
    return rename(tmp, path) == 0;
}

static bool
sd_copy(void *ctx, const char *from, const char *to)
{
    size_t len = 0;
    unsigned char *buf = slurp(from, &len);
    bool ok;

    (void) ctx;
    if (!buf) {
        return false;
    }
    ok = write_whole(to, buf, len);
    free(buf);
    return ok;
}

static bool
sd_sha256(void *ctx, const char *path, unsigned char out[32])
{
    size_t len = 0;
    unsigned char *buf = slurp(path, &len);
    bool ok;

    (void) ctx;
    if (!buf) {
        return false;
    }
    ok = wf_crypto_sha256(buf, len, out) == WF_OK;
    free(buf);
    return ok;
}

static bool
sd_write_state(void *ctx, const char *path, const indigo_update_state *s)
{
    char buf[256];
    size_t len = 0;

    (void) ctx;
    return indigo_update_state_encode(s, buf, sizeof buf, &len) == INDIGO_CODEC_OK &&
           write_whole(path, buf, len);
}

static bool
sd_read_state(void *ctx, const char *path, indigo_update_state *s)
{
    char buf[256];
    size_t n;
    FILE *f = fopen(path, "rb");

    (void) ctx;
    if (!f) {
        return false;
    }
    n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    return indigo_update_state_decode(buf, n, s) == INDIGO_CODEC_OK;
}

const indigo_update_fs *
indigo_update_sd_fs(void)
{
    static const indigo_update_fs fs = {NULL,      sd_exists, sd_rename,      sd_remove,
                                        sd_copy,   sd_sha256, sd_write_state, sd_read_state};
    return &fs;
}

void
indigo_update_sd_recover(const char *argv0, const char *state_path)
{
    indigo_update_paths p;
    char running[INDIGO_UPDATE_VERSION_MAX];
    bool dev = true;
    const char *release = NULL;
    static const char *const names[] = {"nothing to do", "discarded an unfinished download",
                                        "finished an interrupted update",
                                        "restored the previous build", "update confirmed",
                                        "waiting for the new build to start"};

    if (!indigo_update_paths_from(argv0, state_path, &p)) {
        return; /* not launched from a .3dsx on the SD card */
    }
    if (indigo_update_release_of(INDIGO_BUILD_COMMIT, running, sizeof running, &dev) && !dev) {
        release = running;
    }
    indigo_recover_action a = indigo_update_recover(indigo_update_sd_fs(), &p, release, argv0);
    if (a != INDIGO_RECOVER_NOTHING) {
        /* Paths and versions only; nothing here is a credential. */
        indigo_log_info("update: %s", names[a]);
    }
}

#else

void
indigo_update_sd_recover(const char *argv0, const char *state_path)
{
    (void) argv0;
    (void) state_path;
}

#endif
