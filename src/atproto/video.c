#include "atproto/video.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The m3u8 parser is Wolfram's (wf_embed_video_playlist_duration), which is
 * why this module does not carry one: the host tests here cover the format and
 * the master-playlist rule, and Wolfram's own suite covers the parser. */
#if defined(WOLFRAM_3DS)
#include <wolfram/embed_typed.h>
#endif

bool
indigo_video_duration_parse(const char *body, size_t len, double *seconds)
{
    double total = 0.0;

    if (!body || len == 0 || !seconds) {
        return false;
    }
#if defined(WOLFRAM_3DS)
    if (wf_embed_video_playlist_duration(body, len, &total) != WF_OK) {
        return false;
    }
#else
    (void) body;
    (void) len;
    return false;
#endif
    if (!isfinite(total) || total <= 0.0) {
        return false;
    }
    *seconds = total;
    return true;
}

bool
indigo_video_duration_format(double seconds, char *out, size_t out_size)
{
    long total;
    long hours;
    long minutes;
    long secs;
    int n;

    if (!out || out_size == 0) {
        return false;
    }
    out[0] = '\0';
    if (!isfinite(seconds) || seconds < 0.0 || seconds > 1e9) {
        return false;
    }

    total = (long) (seconds + 0.5);
    hours = total / 3600;
    minutes = (total % 3600) / 60;
    secs = total % 60;
    if (hours > 0) {
        n = snprintf(out, out_size, "%ld:%02ld:%02ld", hours, minutes, secs);
    } else {
        n = snprintf(out, out_size, "%ld:%02ld", minutes, secs);
    }
    if (n <= 0 || (size_t) n >= out_size) {
        /* Too long: snprintf has already written a truncated prefix. */
        out[0] = '\0';
        return false;
    }
    return true;
}

/* 9 digits is 999999999, which fits a 32-bit long as the 3DS's is. */
#define BANDWIDTH_DIGITS_MAX 9

/* Appends `len` bytes of `s` to `out` at `*n`, NUL-terminated, or fails when
 * they do not fit. */
static bool
append(char *out, size_t out_size, size_t *n, const char *s, size_t len)
{
    if (len >= out_size || *n > out_size - len - 1) {
        return false;
    }
    memcpy(out + *n, s, len);
    *n += len;
    out[*n] = '\0';
    return true;
}

/* The BANDWIDTH value in an attribute list (the text after
 * "#EXT-X-STREAM-INF:"). A quoted value may hold commas, so the list is walked
 * attribute by attribute rather than split on commas. */
static bool
stream_bandwidth(const char *p, size_t n, long *out)
{
    size_t i = 0;

    while (i < n) {
        size_t key;
        size_t key_len;
        size_t value;
        size_t value_end;

        while (i < n && (p[i] == ',' || p[i] == ' ')) {
            i++;
        }
        key = i;
        while (i < n && p[i] != '=' && p[i] != ',') {
            i++;
        }
        key_len = i - key;
        if (i >= n || p[i] != '=') {
            continue; /* a bare word with no value */
        }
        i++;

        if (i < n && p[i] == '"') {
            value = ++i;
            while (i < n && p[i] != '"') {
                i++;
            }
            value_end = i;
            if (i < n) {
                i++;
            }
        } else {
            value = i;
            while (i < n && p[i] != ',') {
                i++;
            }
            value_end = i;
        }

        if (key_len == 9 && memcmp(p + key, "BANDWIDTH", 9) == 0) {
            long v = 0;
            size_t digits = 0;

            if (value_end == value) {
                return false;
            }
            for (size_t k = value; k < value_end; k++) {
                if (p[k] < '0' || p[k] > '9') {
                    return false;
                }
                digits++;
                if (digits <= BANDWIDTH_DIGITS_MAX) {
                    v = v * 10 + (p[k] - '0');
                }
            }
            if (digits > BANDWIDTH_DIGITS_MAX) {
                v = 999999999L;
            }
            *out = v;
            return true;
        }
    }
    return false;
}

/* Resolves a variant's URI reference against the playlist's own URL. */
static bool
resolve_uri(const char *base, const char *ref, size_t ref_len,
            char *out, size_t out_size)
{
    size_t n = 0;
    const char *scheme_sep;
    size_t after_scheme;
    size_t end;

    out[0] = '\0';

    if ((ref_len >= 8 && memcmp(ref, "https://", 8) == 0) ||
        (ref_len >= 7 && memcmp(ref, "http://", 7) == 0)) {
        return append(out, out_size, &n, ref, ref_len);
    }

    scheme_sep = strstr(base, "://");
    after_scheme = scheme_sep ? (size_t) (scheme_sep - base) + 3 : 0;
    /* The base up to any query or fragment. */
    end = strcspn(base, "?#");

    if (ref_len >= 2 && ref[0] == '/' && ref[1] == '/') {
        /* Scheme-relative: keep the base's scheme, including its colon. */
        if (!scheme_sep) {
            return false;
        }
        return append(out, out_size, &n, base, (size_t) (scheme_sep - base) + 1) &&
               append(out, out_size, &n, ref, ref_len);
    }

    if (ref[0] == '/') {
        /* Root-relative: keep the base's scheme and host. */
        size_t origin = end;

        if (!scheme_sep) {
            return false;
        }
        if (end > after_scheme) {
            const char *slash = memchr(base + after_scheme, '/', end - after_scheme);

            if (slash) {
                origin = (size_t) (slash - base);
            }
        }
        return append(out, out_size, &n, base, origin) &&
               append(out, out_size, &n, ref, ref_len);
    }

    /* Relative: keep the base's directory, which ends at its last slash in the
     * path. A base with no path at all gets one. */
    {
        size_t dir_end = 0;

        for (size_t k = after_scheme; k < end; k++) {
            if (base[k] == '/') {
                dir_end = k + 1;
            }
        }
        if (dir_end) {
            if (!append(out, out_size, &n, base, dir_end)) {
                return false;
            }
        } else if (!append(out, out_size, &n, base, end) ||
                   !append(out, out_size, &n, "/", 1)) {
            return false;
        }
    }
    return append(out, out_size, &n, ref, ref_len);
}

bool
indigo_video_master_variant(const char *body, size_t len, const char *base_uri,
                            char *out, size_t out_size)
{
    static const char tag[] = "#EXT-X-STREAM-INF:";
    const size_t tag_len = sizeof(tag) - 1;
    bool pending = false;
    bool pending_has_bw = false;
    long pending_bw = 0;
    bool have = false;
    bool best_has_bw = false;
    long best_bw = 0;
    size_t best_start = 0;
    size_t best_len = 0;
    size_t pos = 0;

    if (!body || !base_uri || !out || out_size == 0) {
        return false;
    }
    out[0] = '\0';

    /* A variant with a BANDWIDTH beats one without; among those with one the
     * lowest wins; among those without, the first. */
    while (pos < len) {
        size_t end = pos;
        size_t a;
        size_t b;
        bool take;

        while (end < len && body[end] != '\n') {
            end++;
        }
        a = pos;
        b = end;
        pos = end + 1;
        while (b > a && (body[b - 1] == '\r' || body[b - 1] == ' ' || body[b - 1] == '\t')) {
            b--;
        }
        while (a < b && (body[a] == ' ' || body[a] == '\t')) {
            a++;
        }
        if (a == b) {
            continue;
        }

        if (b - a >= tag_len && memcmp(body + a, tag, tag_len) == 0) {
            pending = true;
            pending_bw = 0;
            pending_has_bw = stream_bandwidth(body + a + tag_len,
                                              b - a - tag_len, &pending_bw);
            continue;
        }
        if (body[a] == '#' || !pending) {
            continue; /* another tag, a comment, or a URI with no entry before it */
        }

        /* The URI line that follows a STREAM-INF entry is that variant's URI. */
        pending = false;
        take = !have || (pending_has_bw && (!best_has_bw || pending_bw < best_bw));
        if (take) {
            have = true;
            best_has_bw = pending_has_bw;
            best_bw = pending_bw;
            best_start = a;
            best_len = b - a;
        }
    }

    if (!have) {
        return false;
    }
    return resolve_uri(base_uri, body + best_start, best_len, out, out_size);
}
