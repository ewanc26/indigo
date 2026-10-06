#include "update/update.h"

#include <stdio.h>
#include <string.h>

/* ---- Versions ----------------------------------------------------------- */

static bool
parse_uint(const char **p, unsigned *out)
{
    const char *s = *p;
    unsigned long v = 0;

    if (*s < '0' || *s > '9') {
        return false;
    }
    /* No leading zeros except "0" itself: "01.2.3" is not a version anyone
     * tagged, and accepting it would make two spellings of one release. */
    if (s[0] == '0' && s[1] >= '0' && s[1] <= '9') {
        return false;
    }
    while (*s >= '0' && *s <= '9') {
        v = v * 10u + (unsigned long) (*s - '0');
        if (v > 65535u) {
            return false;
        }
        s++;
    }
    *out = (unsigned) v;
    *p = s;
    return true;
}

bool
indigo_update_release_of(const char *s, char *out, size_t cap, bool *dev)
{
    unsigned major;
    unsigned minor;
    unsigned patch;
    unsigned ahead = 0;
    bool dirty = false;
    int n;

    if (!s || !out || cap == 0) {
        return false;
    }
    out[0] = '\0';
    if (*s == 'v') {
        s++;
    }
    if (!parse_uint(&s, &major) || *s++ != '.' || !parse_uint(&s, &minor) || *s++ != '.' ||
        !parse_uint(&s, &patch)) {
        return false;
    }
    /* git describe: -<ahead>-g<hex> */
    if (s[0] == '-' && s[1] >= '0' && s[1] <= '9') {
        size_t hex = 0;

        s++;
        if (!parse_uint(&s, &ahead) || s[0] != '-' || s[1] != 'g') {
            return false;
        }
        s += 2;
        while ((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f')) {
            s++;
            hex++;
        }
        if (hex < 4) {
            return false;
        }
    }
    if (strcmp(s, "-dirty") == 0) {
        dirty = true;
        s += 6;
    }
    if (*s != '\0') {
        return false;
    }
    n = snprintf(out, cap, "%u.%u.%u", major, minor, patch);
    if (n < 0 || (size_t) n >= cap) {
        out[0] = '\0';
        return false;
    }
    if (dev) {
        *dev = ahead > 0 || dirty;
    }
    return true;
}

/* A plain release number, as the journal and the prefix take it. */
static bool
is_release(const char *v)
{
    char tmp[INDIGO_UPDATE_VERSION_MAX];
    bool dev = true;

    return v && v[0] != 'v' && indigo_update_release_of(v, tmp, sizeof tmp, &dev) && !dev &&
           strcmp(tmp, v) == 0;
}

bool
indigo_update_asset_prefix(const char *version, char *out, size_t cap)
{
    int n;

    if (!out || cap == 0 || !is_release(version)) {
        return false;
    }
    n = snprintf(out, cap, "https://github.com/" INDIGO_UPDATE_REPO "/releases/download/v%s/",
                 version);
    return n > 0 && (size_t) n < cap;
}

bool
indigo_update_asset_ok(const char *version, const char *name, const char *url)
{
    char prefix[INDIGO_UPDATE_URL_MAX];
    char want_name[INDIGO_UPDATE_VERSION_MAX + 16];
    char want_url[INDIGO_UPDATE_URL_MAX + 64];

    if (!name || !url || !indigo_update_asset_prefix(version, prefix, sizeof prefix)) {
        return false;
    }
    snprintf(want_name, sizeof want_name, "indigo-%s.3dsx", version);
    snprintf(want_url, sizeof want_url, "%s%s", prefix, want_name);
    return strcmp(name, want_name) == 0 && strcmp(url, want_url) == 0;
}

/* ---- Hex ---------------------------------------------------------------- */

static int
hexval(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1; /* uppercase is rejected: one spelling per digest */
}

static bool
parse_sha256(const char *hex, size_t len, unsigned char out[32])
{
    if (len != 64) {
        return false;
    }
    for (size_t i = 0; i < 32; i++) {
        int hi = hexval(hex[2 * i]);
        int lo = hexval(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = (unsigned char) (hi << 4 | lo);
    }
    return true;
}

static void
format_sha256(const unsigned char d[32], char out[65])
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; i++) {
        out[2 * i] = digits[d[i] >> 4];
        out[2 * i + 1] = digits[d[i] & 15];
    }
    out[64] = '\0';
}

/* ---- Line codec for the journal ---------------------------------------- */

typedef struct {
    const char *buf;
    size_t len;
    size_t pos;
} lines;

/* Next line without its '\n'. False at the end, or for a final line with no
 * newline -- a truncated file. */
static bool
next_line(lines *l, const char **line, size_t *n)
{
    if (l->pos >= l->len) {
        return false;
    }
    const char *start = l->buf + l->pos;
    const char *nl = memchr(start, '\n', l->len - l->pos);
    if (!nl) {
        return false;
    }
    *line = start;
    *n = (size_t) (nl - start);
    l->pos += *n + 1;
    return true;
}

static bool
key_is(const char *line, size_t n, const char *key, const char **value, size_t *vlen)
{
    size_t k = strlen(key);
    if (n <= k || memcmp(line, key, k) != 0 || line[k] != '=') {
        return false;
    }
    *value = line + k + 1;
    *vlen = n - k - 1;
    return true;
}

static bool
copy_value(const char *v, size_t n, char *out, size_t cap)
{
    if (n + 1 > cap || memchr(v, '\0', n)) {
        return false;
    }
    memcpy(out, v, n);
    out[n] = '\0';
    return true;
}

#define STATE_MAGIC "indigo-update-state 1"

/* ---- Journal ------------------------------------------------------------ */

static const char *const k_phase_names[] = {"none", "staged", "swapping", "swapped"};

indigo_codec_status
indigo_update_state_decode(const char *buf, size_t len, indigo_update_state *out)
{
    lines l = {buf, len, 0};
    const char *line;
    size_t n;
    const char *v;
    size_t vn;
    char tmp[32];
    indigo_update_state s;
    unsigned seen = 0;

    if (!buf || !out) {
        return INDIGO_CODEC_CORRUPT;
    }
    if (len == 0) {
        return INDIGO_CODEC_EMPTY;
    }
    memset(&s, 0, sizeof s);
    if (!next_line(&l, &line, &n) || n != sizeof STATE_MAGIC - 1 ||
        memcmp(line, STATE_MAGIC, n) != 0) {
        return INDIGO_CODEC_CORRUPT;
    }
    for (;;) {
        if (!next_line(&l, &line, &n)) {
            return INDIGO_CODEC_INCOMPLETE;
        }
        if (n == 3 && memcmp(line, "end", 3) == 0) {
            break;
        }
        if (key_is(line, n, "phase", &v, &vn)) {
            unsigned i;
            for (i = 1; i < 4; i++) {
                if (vn == strlen(k_phase_names[i]) && memcmp(v, k_phase_names[i], vn) == 0) {
                    break;
                }
            }
            if (i == 4 || (seen & 1)) {
                return INDIGO_CODEC_CORRUPT;
            }
            s.phase = (indigo_update_phase) i;
            seen |= 1;
        } else if (key_is(line, n, "version", &v, &vn)) {
            if ((seen & 2) || !copy_value(v, vn, tmp, sizeof tmp) || !is_release(tmp)) {
                return INDIGO_CODEC_CORRUPT;
            }
            memcpy(s.version, tmp, strlen(tmp) + 1);
            seen |= 2;
        } else if (key_is(line, n, "sha256", &v, &vn)) {
            if ((seen & 4) || !parse_sha256(v, vn, s.sha256)) {
                return INDIGO_CODEC_CORRUPT;
            }
            seen |= 4;
        } else {
            return INDIGO_CODEC_CORRUPT;
        }
    }
    if (seen != 7) {
        return INDIGO_CODEC_INCOMPLETE;
    }
    *out = s;
    return INDIGO_CODEC_OK;
}

indigo_codec_status
indigo_update_state_encode(const indigo_update_state *s, char *buf, size_t cap, size_t *len)
{
    char hex[65];
    int n;

    if (!s || !buf || !len || s->phase == INDIGO_UPDATE_PHASE_NONE ||
        (unsigned) s->phase > INDIGO_UPDATE_PHASE_SWAPPED || !is_release(s->version)) {
        return INDIGO_CODEC_CORRUPT;
    }
    format_sha256(s->sha256, hex);
    n = snprintf(buf, cap, STATE_MAGIC "\nphase=%s\nversion=%s\nsha256=%s\nend\n",
                 k_phase_names[s->phase], s->version, hex);
    if (n < 0 || (size_t) n >= cap) {
        return INDIGO_CODEC_TOO_BIG;
    }
    *len = (size_t) n;
    return INDIGO_CODEC_OK;
}

/* ---- Paths -------------------------------------------------------------- */

bool
indigo_update_paths_from(const char *argv0, const char *state, indigo_update_paths *out)
{
    static const char ext[] = ".3dsx";
    size_t n;
    int w;

    if (!argv0 || !state || !out) {
        return false;
    }
    memset(out, 0, sizeof *out);
    n = strlen(argv0);
    if (strncmp(argv0, "sdmc:/", 6) != 0 || n <= 6 + sizeof ext - 1 ||
        strcmp(argv0 + n - (sizeof ext - 1), ext) != 0 || strstr(argv0, "/../") ||
        n + 16 >= INDIGO_UPDATE_PATH_MAX) {
        return false;
    }
    w = snprintf(out->target, sizeof out->target, "%s", argv0);
    if (w < 0 || (size_t) w >= sizeof out->target) {
        return false;
    }
    w = snprintf(out->staged, sizeof out->staged, "%s.new", argv0);
    if (w < 0 || (size_t) w >= sizeof out->staged) {
        return false;
    }
    /* The backup keeps the .3dsx extension so the Homebrew Menu lists it: if
     * the console dies between the two renames, the person still has a build
     * to launch, and launching it finishes the job. */
    w = snprintf(out->backup, sizeof out->backup, "%.*s-previous%s",
                 (int) (n - (sizeof ext - 1)), argv0, ext);
    if (w < 0 || (size_t) w >= sizeof out->backup) {
        return false;
    }
    w = snprintf(out->state, sizeof out->state, "%s", state);
    return w > 0 && (size_t) w < sizeof out->state;
}

/* ---- Swap and recovery -------------------------------------------------- */

static bool
write_phase(const indigo_update_fs *fs, const indigo_update_paths *p,
            const indigo_update_state *base, indigo_update_phase phase)
{
    indigo_update_state s = *base;
    s.phase = phase;
    return fs->write_state(fs->ctx, p->state, &s);
}

indigo_update_result
indigo_update_stage(const indigo_update_fs *fs, const indigo_update_paths *p,
                    const char *version, const unsigned char sha256[32])
{
    unsigned char got[32];
    indigo_update_state s;

    if (!is_release(version) || !sha256) {
        return INDIGO_UPDATE_NOT_STAGED;
    }
    if (!fs->exists(fs->ctx, p->staged) || !fs->sha256(fs->ctx, p->staged, got) ||
        memcmp(got, sha256, 32) != 0) {
        fs->remove(fs->ctx, p->staged);
        return INDIGO_UPDATE_NOT_STAGED;
    }
    memset(&s, 0, sizeof s);
    memcpy(s.version, version, strlen(version) + 1);
    memcpy(s.sha256, sha256, 32);
    return write_phase(fs, p, &s, INDIGO_UPDATE_PHASE_STAGED) ? INDIGO_UPDATE_OK
                                                              : INDIGO_UPDATE_IO;
}

indigo_update_result
indigo_update_install(const indigo_update_fs *fs, const indigo_update_paths *p)
{
    indigo_update_state s;

    if (!fs->read_state(fs->ctx, p->state, &s) || s.phase != INDIGO_UPDATE_PHASE_STAGED ||
        !fs->exists(fs->ctx, p->staged)) {
        return INDIGO_UPDATE_NOT_STAGED;
    }
    if (!write_phase(fs, p, &s, INDIGO_UPDATE_PHASE_SWAPPING)) {
        return INDIGO_UPDATE_IO;
    }
    if (fs->exists(fs->ctx, p->backup) && !fs->remove(fs->ctx, p->backup)) {
        return INDIGO_UPDATE_IO;
    }
    if (!fs->rename(fs->ctx, p->target, p->backup) ||
        !fs->rename(fs->ctx, p->staged, p->target) ||
        !write_phase(fs, p, &s, INDIGO_UPDATE_PHASE_SWAPPED)) {
        return INDIGO_UPDATE_IO;
    }
    return INDIGO_UPDATE_OK;
}

static bool
hash_is(const indigo_update_fs *fs, const char *path, const unsigned char want[32])
{
    unsigned char got[32];
    return fs->sha256(fs->ctx, path, got) && memcmp(got, want, 32) == 0;
}

/* Put the backup back at the target. The running build may have been
 * launched from the backup, and its RomFS reads that file, so in that case it
 * is copied rather than moved. */
static bool
restore_backup(const indigo_update_fs *fs, const indigo_update_paths *p, const char *running_path)
{
    if (!fs->exists(fs->ctx, p->backup)) {
        return false;
    }
    if (running_path && strcmp(running_path, p->backup) == 0) {
        return fs->copy(fs->ctx, p->backup, p->target);
    }
    return fs->rename(fs->ctx, p->backup, p->target);
}

indigo_recover_action
indigo_update_recover(const indigo_update_fs *fs, const indigo_update_paths *p,
                      const char *running, const char *running_path)
{
    indigo_update_state s;
    bool have_state = fs->read_state(fs->ctx, p->state, &s);
    bool t;
    bool n;

    if (!have_state) {
        memset(&s, 0, sizeof s);
    }
    t = fs->exists(fs->ctx, p->target);
    n = fs->exists(fs->ctx, p->staged);

    switch (s.phase) {
    case INDIGO_UPDATE_PHASE_NONE:
        /* A download that never reached the journal; the backup, if any, is
         * left alone, because without a journal nothing says it is spare. */
        if (n && fs->remove(fs->ctx, p->staged)) {
            fs->remove(fs->ctx, p->state);
            return INDIGO_RECOVER_DISCARDED_STAGED;
        }
        if (have_state || fs->exists(fs->ctx, p->state)) {
            fs->remove(fs->ctx, p->state);
        }
        return INDIGO_RECOVER_NOTHING;

    case INDIGO_UPDATE_PHASE_STAGED:
        /* The swap never started; the running build is intact. Throw the
         * download away rather than install something nobody confirmed. */
        if (n && !fs->remove(fs->ctx, p->staged)) {
            return INDIGO_RECOVER_NOTHING;
        }
        fs->remove(fs->ctx, p->state);
        return INDIGO_RECOVER_DISCARDED_STAGED;

    case INDIGO_UPDATE_PHASE_SWAPPING:
        if (t && n) {
            /* Stopped before the target moved: the old build is in place. */
            if (fs->remove(fs->ctx, p->staged)) {
                fs->remove(fs->ctx, p->state);
            }
            return INDIGO_RECOVER_DISCARDED_STAGED;
        }
        if (!t && n) {
            /* Between the two renames. */
            if (hash_is(fs, p->staged, s.sha256)) {
                if (fs->rename(fs->ctx, p->staged, p->target) &&
                    write_phase(fs, p, &s, INDIGO_UPDATE_PHASE_SWAPPED)) {
                    return INDIGO_RECOVER_FINISHED_SWAP;
                }
                return INDIGO_RECOVER_NOTHING;
            }
            if (restore_backup(fs, p, running_path) && fs->remove(fs->ctx, p->staged)) {
                fs->remove(fs->ctx, p->state);
                return INDIGO_RECOVER_RESTORED_BACKUP;
            }
            return INDIGO_RECOVER_NOTHING;
        }
        if (t && !n) {
            /* Both renames done; only the journal update was lost. */
            if (hash_is(fs, p->target, s.sha256)) {
                return write_phase(fs, p, &s, INDIGO_UPDATE_PHASE_SWAPPED)
                           ? INDIGO_RECOVER_FINISHED_SWAP
                           : INDIGO_RECOVER_NOTHING;
            }
            /* The target is not the release, so it is still the old build:
             * the backup was never made. */
            fs->remove(fs->ctx, p->state);
            return INDIGO_RECOVER_NOTHING;
        }
        /* Neither: the target moved and the staged file is gone. */
        if (restore_backup(fs, p, running_path)) {
            fs->remove(fs->ctx, p->state);
            return INDIGO_RECOVER_RESTORED_BACKUP;
        }
        return INDIGO_RECOVER_NOTHING;

    case INDIGO_UPDATE_PHASE_SWAPPED:
        if (!t) {
            if (restore_backup(fs, p, running_path)) {
                fs->remove(fs->ctx, p->state);
                return INDIGO_RECOVER_RESTORED_BACKUP;
            }
            return INDIGO_RECOVER_NOTHING;
        }
        /* The new build has started from the target: it works well enough to
         * get here, so the old one can go. */
        if (running && strcmp(running, s.version) == 0 && running_path &&
            strcmp(running_path, p->target) == 0) {
            if (!fs->exists(fs->ctx, p->backup) || fs->remove(fs->ctx, p->backup)) {
                fs->remove(fs->ctx, p->state);
                return INDIGO_RECOVER_CONFIRMED;
            }
            return INDIGO_RECOVER_NOTHING;
        }
        return INDIGO_RECOVER_WAITING;
    }
    return INDIGO_RECOVER_NOTHING;
}
