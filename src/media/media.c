#include "media/media.h"

#include "gfx/canvas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
slot_clear(indigo_media_slot *slot)
{
    free(slot->pixels);
    memset(slot, 0, sizeof *slot);
}

void
indigo_media_init(indigo_media_cache *c)
{
    memset(c, 0, sizeof *c);
}

void
indigo_media_clear(indigo_media_cache *c)
{
    for (unsigned i = 0; i < INDIGO_MEDIA_SLOTS; i++) {
        slot_clear(&c->slots[i]);
    }
    /* The counters are diagnostics rather than state, so a clear leaves them
     * alone; only what is resident goes back to zero. */
    c->bytes = 0;
}

static int
find(const indigo_media_cache *c, const char *url)
{
    for (unsigned i = 0; i < INDIGO_MEDIA_SLOTS; i++) {
        if (c->slots[i].state != INDIGO_MEDIA_EMPTY &&
            strcmp(c->slots[i].url, url) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* The slot to reuse: a failed one first (it is holding memory nobody will ask
 * for again), then an empty one, then the least recently used ready one. A
 * loading slot is never a candidate -- taking one would throw away a request
 * that has already been paid for, and a fast scroll would then never settle. */
static int
victim(const indigo_media_cache *c)
{
    int best = -1;
    int best_failed = -1;

    for (unsigned i = 0; i < INDIGO_MEDIA_SLOTS; i++) {
        const indigo_media_slot *s = &c->slots[i];

        if (s->state == INDIGO_MEDIA_EMPTY) {
            return (int)i;
        }
        if (s->state == INDIGO_MEDIA_FAILED) {
            if (best_failed < 0 || s->last_used < c->slots[best_failed].last_used) {
                best_failed = (int)i;
            }
        }
    }
    if (best_failed >= 0) {
        return best_failed;
    }
    for (unsigned i = 0; i < INDIGO_MEDIA_SLOTS; i++) {
        const indigo_media_slot *s = &c->slots[i];

        if (s->state != INDIGO_MEDIA_READY) {
            continue;
        }
        if (best < 0 || s->last_used < c->slots[best].last_used) {
            best = (int)i;
        }
    }
    return best;
}

bool
indigo_media_known(const indigo_media_cache *c, const char *url)
{
    if (!url || url[0] == '\0') {
        return false;
    }
    return find(c, url) >= 0;
}

int
indigo_media_ready(indigo_media_cache *c, const char *url)
{
    int i;

    if (!c || !url || url[0] == '\0') {
        return -1;
    }
    i = find(c, url);
    if (i < 0 || c->slots[i].state != INDIGO_MEDIA_READY) {
        c->misses++;
        return -1;
    }
    c->slots[i].last_used = ++c->clock;
    c->hits++;
    return i;
}

int
indigo_media_claim(indigo_media_cache *c, const char *url, unsigned *generation)
{
    size_t len;
    int i;

    if (generation) {
        *generation = 0;
    }
    if (!c || !url || url[0] == '\0' || !generation) {
        return -1;
    }
    len = strlen(url);
    if (len >= INDIGO_MEDIA_URL_MAX) {
        return -1;
    }
    if (find(c, url) >= 0) {
        return -1;
    }

    i = victim(c);
    if (i < 0) {
        return -1; /* every slot is in flight */
    }
    if (c->slots[i].state != INDIGO_MEDIA_EMPTY) {
        c->bytes -= c->slots[i].bytes;
        c->evictions++;
    }
    slot_clear(&c->slots[i]);
    snprintf(c->slots[i].url, sizeof c->slots[i].url, "%s", url);
    c->slots[i].state = INDIGO_MEDIA_LOADING;
    c->slots[i].last_used = ++c->clock;
    c->generation++;
    if (c->generation == 0) {
        c->generation++; /* 0 is the "no generation" sentinel */
    }
    c->slots[i].generation = c->generation;
    *generation = c->generation;
    c->loads++;
    return i;
}

/* Frees ready/empty space until `needed` more bytes fit, never touching the
 * slot being published into or anything still in flight. False when even every
 * evictable slot is gone and it still does not fit. */
static bool
make_room(indigo_media_cache *c, int keep, unsigned needed)
{
    while (c->bytes + needed > INDIGO_MEDIA_BYTES_MAX) {
        int best = -1;

        for (unsigned i = 0; i < INDIGO_MEDIA_SLOTS; i++) {
            indigo_media_slot *s = &c->slots[i];

            if ((int)i == keep || s->state != INDIGO_MEDIA_READY) {
                continue;
            }
            if (best < 0 || s->last_used < c->slots[best].last_used) {
                best = (int)i;
            }
        }
        if (best < 0) {
            return false;
        }
        c->bytes -= c->slots[best].bytes;
        slot_clear(&c->slots[best]);
        c->evictions++;
    }
    return true;
}

bool
indigo_media_publish(indigo_media_cache *c, int slot, unsigned generation,
                     uint8_t *pixels, unsigned width, unsigned height)
{
    indigo_media_slot *s;

    if (!c || !pixels || width == 0 || height == 0 || slot < 0 ||
        slot >= (int)INDIGO_MEDIA_SLOTS) {
        return false;
    }
    s = &c->slots[slot];
    if (s->state != INDIGO_MEDIA_LOADING || s->generation != generation) {
        return false; /* evicted and reused while the fetch was in flight */
    }
    if (width > INDIGO_MEDIA_MAX_DIM || height > INDIGO_MEDIA_MAX_DIM) {
        return false; /* the decoder ignored its own cap */
    }
    if ((size_t)width * (size_t)height > (size_t)-1 / 4u) {
        return false;
    }
    if (!make_room(c, slot, width * height * 4u)) {
        return false;
    }
    s->pixels = pixels;
    s->width = width;
    s->height = height;
    s->bytes = width * height * 4u;
    s->state = INDIGO_MEDIA_READY;
    s->last_used = ++c->clock;
    c->bytes += s->bytes;
    return true;
}

void
indigo_media_fail(indigo_media_cache *c, int slot, unsigned generation)
{
    indigo_media_slot *s;

    if (!c || slot < 0 || slot >= (int)INDIGO_MEDIA_SLOTS) {
        return;
    }
    s = &c->slots[slot];
    if (s->state != INDIGO_MEDIA_LOADING || s->generation != generation) {
        return;
    }
    s->state = INDIGO_MEDIA_FAILED;
    s->last_used = ++c->clock;
    c->failures++;
}

uint32_t
indigo_media_placeholder_color(const char *url)
{
    /* FNV-1a, then a hue from it at fixed saturation and value. Both fixed on
     * purpose: the placeholder has to sit inside the palette rather than
     * introduce colours of its own, while the hue alone keeps two accounts
     * distinguishable at a glance. */
    unsigned h = 2166136261u;
    unsigned hue, sat, value, c, x, m, r, g, b;

    for (const unsigned char *p = (const unsigned char *)url; p && *p; p++) {
        h ^= *p;
        h *= 16777619u;
    }
    hue = h % 360u;
    sat = 96u;
    value = 132u;

    c = value * sat / 255u;
    /* Position within the hue's 60-degree sector, in 1/1024ths, so a hue that
     * lands near a sector boundary is not flattened onto the boundary: with a
     * binary choice instead of an interpolation there are only six possible
     * colours, and two accounts share one often enough to defeat the point. */
    x = c * (((hue % 60u) * 1024u) / 60u) / 1024u;
    m = value - c;
    r = 0;
    g = 0;
    b = 0;
    switch (hue / 60u) {
    case 0: r = c; g = x; break;
    case 1: r = x; g = c; break;
    case 2: g = c; b = x; break;
    case 3: g = x; b = c; break;
    case 4: r = x; b = c; break;
    default: r = c; b = x; break;
    }
    if (hue % 60u >= 30u) {
        /* Second half of the sector: the ramp runs the other way. */
        x = c - x;
        switch (hue / 60u) {
        case 0: g = x; break;
        case 1: r = x; break;
        case 2: b = x; break;
        case 3: g = x; break;
        case 4: r = x; break;
        default: b = x; break;
        }
    }
    return INDIGO_RGBA(r + m, g + m, b + m, 255);
}
