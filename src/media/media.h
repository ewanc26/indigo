#ifndef INDIGO_MEDIA_H
#define INDIGO_MEDIA_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The decoded-image cache. Indigo has no image pipeline of its own until this:
 * an avatar or a post thumbnail arrives as a URL on a view struct, and before
 * src/media/ existed that URL simply had nowhere to go (see the note that used
 * to sit on indigo_actor).
 *
 * The split is deliberate:
 *
 *   - this file is the cache, and it is pure. No libctru, no citro2d, no
 *     Wolfram, no network. Every state transition is a function of its
 *     arguments, which is what makes the eviction and generation rules
 *     testable on the host at all.
 *   - media_loader.c owns the thread that fetches and decodes, and hands
 *     finished images back through indigo_media_drain().
 *
 * Consequently this cache is touched by the main thread only. The loader never
 * writes here; it publishes into its own ring, and the main thread adopts
 * results once per frame. That is what keeps a decode finishing mid-frame from
 * being able to pull a slot out from under the code drawing it.
 *
 * Everything is fixed-size and allocation-free except the decoded pixels
 * themselves, which are owned by the slot and freed on eviction, on clear and
 * on adoption of a replacement.
 */

/* Slots, and therefore how many images can be on screen at once. A timeline
 * row shows three posts, the thread view about eight rows deep, so 24 covers
 * every screen with room to spare and bounds the worst case. */
#define INDIGO_MEDIA_SLOTS 24

/* Longest CDN URL kept. Bluesky avatar and thumbnail URLs run around 60-90
 * characters; the extra room is not for them but so that a longer URL is
 * stored than rejected, and a rejected avatar is a visibly missing face. */
#define INDIGO_MEDIA_URL_MAX 128

/* Longest decoded side. Avatars are drawn at 32-40px and this is deliberately
 * more than that, so the texture survives being scaled down on screen rather
 * than being decoded at the size it is drawn at and then blurred. */
#define INDIGO_MEDIA_MAX_DIM 96

/* Total decoded bytes across every slot. INDIGO_MEDIA_SLOTS at the dimension
 * cap above would be ~860KB, which the 3DS can hold but should not spend on
 * avatars; this budget is the real bound and it is what stops a screen full of
 * larger thumbnails from quietly growing. */
#define INDIGO_MEDIA_BYTES_MAX (512u * 1024u)

/* Cap on one downloaded body. A CDN thumbnail is tens of kilobytes; anything
 * past this is not a thumbnail, and the URL came from a server, so the fetch
 * is aborted rather than buffered to find out how big it gets. */
#define INDIGO_MEDIA_DOWNLOAD_MAX (256u * 1024u)

typedef enum {
    INDIGO_MEDIA_EMPTY = 0,
    INDIGO_MEDIA_LOADING,
    INDIGO_MEDIA_READY,
    INDIGO_MEDIA_FAILED,
} indigo_media_state;

typedef struct {
    indigo_media_state state;
    /* Bumped every time the slot is claimed or reused. A result carrying an
     * older generation belongs to a request nobody is waiting for any more --
     * the slot was evicted and reused while the fetch was in flight -- and is
     * dropped rather than written over whatever took its place. */
    unsigned generation;
    /* Monotonic tick of the last lookup, for least-recently-used eviction. */
    unsigned last_used;
    char url[INDIGO_MEDIA_URL_MAX];
    /* RGBA8, tightly packed, first row at the top. Owned by the slot. */
    uint8_t *pixels;
    unsigned width;
    unsigned height;
    unsigned bytes;
} indigo_media_slot;

typedef struct {
    indigo_media_slot slots[INDIGO_MEDIA_SLOTS];
    unsigned clock;
    /* Handed out to callers as a generation, so two claims of the same slot
     * can be told apart without the caller holding a slot pointer. */
    unsigned generation;
    unsigned bytes;
    /* Counters. Hits and misses are lookups; loads and failures are fetches.
     * A screen whose misses climb while loads do not means the loader is not
     * keeping up, which looks identical to "nobody has set an avatar" from the
     * outside -- the same argument Cobalt makes for putting fetch counts on its
     * diagnostics screen. */
    unsigned hits;
    unsigned misses;
    unsigned loads;
    unsigned failures;
    unsigned evictions;
} indigo_media_cache;

void indigo_media_init(indigo_media_cache *c);

/* Frees every decoded image. Use on sign-out and on a screen change that would
 * otherwise leave a feed's worth of avatars resident for nothing. */
void indigo_media_clear(indigo_media_cache *c);

/* Index of the READY slot holding `url`, or -1. Touches the slot it finds, so
 * the avatars on screen are not the ones evicted next. */
int indigo_media_ready(indigo_media_cache *c, const char *url);

/* Claims a slot for `url` and returns its index, marking it LOADING. Returns -1
 * when the URL is empty, longer than the cache keeps, already known (loading,
 * ready or failed), or when claiming it would take the cache past
 * INDIGO_MEDIA_BYTES_MAX. A refusal is not an error: it is the cache declining
 * to hold more, and the caller is expected to carry on without an image.
 *
 * The returned generation must be passed back to indigo_media_publish() or
 * indigo_media_fail() for that slot. */
int indigo_media_claim(indigo_media_cache *c, const char *url, unsigned *generation);

/* Adopts decoded pixels into a claimed slot. Takes ownership only when it
 * returns true: on false -- a stale generation, or pixels that do not fit the
 * budget -- the caller still owns `pixels` and must free it. */
bool indigo_media_publish(indigo_media_cache *c, int slot, unsigned generation,
                          uint8_t *pixels, unsigned width, unsigned height);

/* Records that a claimed slot's fetch failed. The slot stays FAILED rather
 * than going back to EMPTY so the same broken URL is not re-requested every
 * frame; indigo_media_clear() is what makes it eligible again. */
void indigo_media_fail(indigo_media_cache *c, int slot, unsigned generation);

/* True when the cache has a slot for `url` in any state. Cheap enough to ask
 * per image command per frame, which is how the backend decides whether to
 * ask the loader for it. */
bool indigo_media_known(const indigo_media_cache *c, const char *url);

/* A stable colour for a URL, for the placeholder drawn while the real image is
 * still loading or never arrives. A column of identical grey squares reads as
 * one voice; a per-post tint does not. Deterministic, so the placeholder does
 * not change between frames. */
uint32_t indigo_media_placeholder_color(const char *url);

#endif