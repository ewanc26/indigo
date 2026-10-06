#ifndef INDIGO_UPDATE_H
#define INDIGO_UPDATE_H

/* Self-update: the parts that decide, kept apart from the parts that touch the
 * network and the SD card so every decision can be tested on the host.
 *
 *   - which release a build is, from its git-describe stamp;
 *   - the one URL prefix an asset may come from;
 *   - the swap and the recovery after an interrupted swap, written against a
 *     small filesystem interface so a test can stop the swap after any step.
 *
 * The manifest (`update.json`, a release asset), its parsing, version
 * ordering and SHA-256 are the shared contract in wolfram#106 and are not
 * written here; Indigo adopts them when Wolfram releases them. See
 * docs/UPDATE.md.
 */

#include "store/store_status.h"

#include <stdbool.h>
#include <stddef.h>

#define INDIGO_UPDATE_REPO "ewanc26/indigo"
#define INDIGO_UPDATE_ASSET "indigo.3dsx"
#define INDIGO_UPDATE_MANIFEST_ASSET "update.json"
/* GitHub's documented "latest release asset" redirect. */
#define INDIGO_UPDATE_MANIFEST_URL \
    "https://github.com/" INDIGO_UPDATE_REPO "/releases/latest/download/" INDIGO_UPDATE_MANIFEST_ASSET
/* Its detached Ed25519 signature; see update_sig.h. */
#define INDIGO_UPDATE_SIGNATURE_URL \
    "https://github.com/" INDIGO_UPDATE_REPO "/releases/latest/download/update.json.sig"
/* The 0.5.0 .3dsx is 1.4MB; this is a ceiling for a sane build, not a target. */
#define INDIGO_UPDATE_MAX_BYTES (8u * 1024u * 1024u)
#define INDIGO_UPDATE_URL_MAX 160u
#define INDIGO_UPDATE_PATH_MAX 256u

#define INDIGO_UPDATE_VERSION_MAX 24u

/* The release a build was made from, as plain "x.y.z", from the build's
 * `git describe` stamp. Accepts "v0.5.0", "v0.5.0-3-gabc1234",
 * "v0.5.0-dirty" and "v0.5.0-3-gabc1234-dirty"; `*dev` is set when the build
 * is past the tag or dirty. A bare commit hash -- what `describe --always`
 * gives a build with no tag in reach -- is rejected, and such a build is
 * never offered an update, because nothing says what it is.
 *
 * Ordering two versions is Wolfram's (wolfram#106): x.y.z precedence is the
 * same for every client and is not re-implemented here. A dev build compares
 * as the release it was built on, so it is never offered that release. */
bool indigo_update_release_of(const char *describe, char *out, size_t cap, bool *dev);

/* The only URL prefix an asset may be fetched from for release `version`
 * ("x.y.z"): https://github.com/ewanc26/indigo/releases/download/v<version>/
 * Handed to Wolfram's manifest parser as its allowed prefix, so a manifest
 * cannot point the console at another host. */
bool indigo_update_asset_prefix(const char *version, char *out, size_t cap);

/* Whether a manifest's asset is the one for release `version` and nothing
 * else: the name is exactly indigo-<version>.3dsx and the URL is exactly the
 * prefix above plus that name. Wolfram's parser enforces a prefix of the
 * caller's choosing before the version is known; this is the second, exact
 * check once it is. */
bool indigo_update_asset_ok(const char *version, const char *name, const char *url);

/* ---- The swap ---------------------------------------------------------- */

typedef struct {
    char target[INDIGO_UPDATE_PATH_MAX];  /* the running .3dsx (argv[0]) */
    char staged[INDIGO_UPDATE_PATH_MAX];  /* target + ".new" */
    char backup[INDIGO_UPDATE_PATH_MAX];  /* <dir>/<stem>-previous.3dsx */
    char state[INDIGO_UPDATE_PATH_MAX];   /* the journal */
} indigo_update_paths;

/* Derive the paths from argv[0]. False -- and self-update unavailable -- when
 * the build was not launched from a .3dsx on the SD card (a CIA, 3dslink, an
 * empty argv). `state` is the journal path, normally under sdmc:/3ds/indigo. */
bool indigo_update_paths_from(const char *argv0, const char *state, indigo_update_paths *out);

typedef enum {
    INDIGO_UPDATE_PHASE_NONE = 0,
    INDIGO_UPDATE_PHASE_STAGED,    /* staged file written and re-verified from disk */
    INDIGO_UPDATE_PHASE_SWAPPING,  /* renames in progress */
    INDIGO_UPDATE_PHASE_SWAPPED,   /* new build in place; backup kept until it boots */
} indigo_update_phase;

typedef struct {
    indigo_update_phase phase;
    char version[INDIGO_UPDATE_VERSION_MAX]; /* the release being installed, x.y.z */
    unsigned char sha256[32];
} indigo_update_state;

indigo_codec_status indigo_update_state_decode(const char *buf, size_t len,
                                               indigo_update_state *out);
indigo_codec_status indigo_update_state_encode(const indigo_update_state *s, char *buf,
                                               size_t cap, size_t *len);

/* The filesystem the swap runs against. Each call returns true on success.
 * The 3DS implementation is stdio over sdmc:; the tests use a fake that can
 * fail every call after the Nth, which is what an unplugged console does. */
typedef struct {
    void *ctx;
    bool (*exists)(void *ctx, const char *path);
    bool (*rename)(void *ctx, const char *from, const char *to);
    bool (*remove)(void *ctx, const char *path);
    bool (*copy)(void *ctx, const char *from, const char *to);
    /* Hash a file; false if it cannot be read. */
    bool (*sha256)(void *ctx, const char *path, unsigned char out[32]);
    bool (*write_state)(void *ctx, const char *path, const indigo_update_state *s);
    /* NONE when the journal is missing or damaged. */
    bool (*read_state)(void *ctx, const char *path, indigo_update_state *s);
} indigo_update_fs;

typedef enum {
    INDIGO_UPDATE_OK = 0,
    INDIGO_UPDATE_IO,          /* a step failed; recover() will tidy up */
    INDIGO_UPDATE_NOT_STAGED,  /* no verified staged file to install */
} indigo_update_result;

/* Record that `paths->staged` holds release `version` with digest `sha256`
 * (both from the manifest Wolfram parsed). The caller has already written the
 * file; this re-hashes it from disk, because a successful write and the bytes
 * on the card are different claims. A mismatch removes the staged file. */
indigo_update_result indigo_update_stage(const indigo_update_fs *fs, const indigo_update_paths *p,
                                         const char *version, const unsigned char sha256[32]);

/* Put the staged build in place of the running one, keeping the running one
 * as the backup. The running build must have closed its RomFS first, and must
 * exit afterwards: the file it was launched from is now the backup. */
indigo_update_result indigo_update_install(const indigo_update_fs *fs,
                                           const indigo_update_paths *p);

typedef enum {
    INDIGO_RECOVER_NOTHING = 0,
    INDIGO_RECOVER_DISCARDED_STAGED,  /* an unfinished download was removed */
    INDIGO_RECOVER_FINISHED_SWAP,     /* the new build was put in place */
    INDIGO_RECOVER_RESTORED_BACKUP,   /* the old build was put back */
    INDIGO_RECOVER_CONFIRMED,         /* the new build booted; backup removed */
    INDIGO_RECOVER_WAITING,           /* swapped, but this is not the new build */
} indigo_recover_action;

/* Run once at start-up, before RomFS is mounted, whichever build is running.
 * Leaves the target holding a build whose hash is known good, or the old one,
 * and never deletes the last copy of anything. `running` may be NULL for a
 * build with no parseable version. `running_path` is argv[0]: recovery never
 * renames the file the running build was launched from, it copies it.
 * `running` is the running build's release ("x.y.z", from
 * indigo_update_release_of), or NULL when it has none or is a dev build. */
indigo_recover_action indigo_update_recover(const indigo_update_fs *fs,
                                            const indigo_update_paths *p,
                                            const char *running,
                                            const char *running_path);

#endif
