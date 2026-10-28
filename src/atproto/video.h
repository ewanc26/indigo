#ifndef INDIGO_VIDEO_H
#define INDIGO_VIDEO_H

/*
 * A video post's length, read from its HLS playlist.
 *
 * app.bsky.embed.video#view carries no duration. Its `playlist` is an m3u8
 * playlist and the length is the sum of its #EXTINF lines; Wolfram parses the
 * body (wf_embed_video_playlist_duration) and does no I/O, so the fetch lives
 * in the session worker.
 *
 * This module is pure: no network, no libctru, no Wolfram state. That is what
 * lets the parser, the formatter and the master-playlist rule be tested on the
 * host, where the console's network is not.
 */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* "h:mm:ss" plus NUL, with room to spare. */
#define INDIGO_VIDEO_DURATION_TEXT_MAX 16
/* A playlist URI from a PDS response; the same bound Cobalt uses. */
#define INDIGO_VIDEO_PLAYLIST_MAX 256

/* The duration of a finished m3u8 playlist, in seconds. False when Wolfram
 * cannot give one (a master playlist, a live playlist, or not a playlist). */
bool indigo_video_duration_parse(const char *body, size_t len, double *seconds);

/* "m:ss" under an hour, "h:mm:ss" from an hour, rounded to the nearest second.
 * False (leaving `out` empty) for a negative or non-finite value, or when the
 * text would not fit. */
bool indigo_video_duration_format(double seconds, char *out, size_t out_size);

/*
 * The variant to follow in a master (multivariant) m3u8 body: the
 * #EXT-X-STREAM-INF entry with the lowest BANDWIDTH, or the first entry when
 * none has one. Its URI is resolved against `base_uri`, the playlist's own
 * URL, so an absolute URI passes through and a relative or root-relative one
 * is joined to it. False when the body has no STREAM-INF entry with a URI, or
 * the result does not fit `out`. Pure: no I/O.
 */
bool indigo_video_master_variant(const char *body, size_t len,
                                 const char *base_uri,
                                 char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* INDIGO_VIDEO_H */
