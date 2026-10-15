#ifndef INDIGO_STORE_CACHE_CODEC_H
#define INDIGO_STORE_CACHE_CODEC_H

/* Timeline cache codec: the last page of the home timeline, kept on the SD
 * card so a cold start has something to show before the network answers.
 *
 * It is a cache, not state. Nothing here is a credential, a draft or a
 * pending mutation, and a missing or damaged file costs only the first paint.
 * The same rules as settings_codec apply: a version marker at the top, values
 * that cannot contain a line break, and a damaged file set aside rather than
 * deleted. Unlike settings, a single bad post does not salvage the rest: a
 * cache with a hole in it is not worth showing, so any corrupt line fails the
 * whole file.
 *
 * File format v1:
 *
 *     indigo-cache 1
 *     post<TAB>uri<TAB>cid<TAB>handle<TAB>display_name<TAB>text
 *     ...
 *     end
 *
 * uri, cid and handle are AT URIs, CIDs and handles, which never contain a
 * tab or line break, so they are written as they are. display_name and text
 * are free text, so backslash, newline, carriage return and tab are written
 * as \\, \n, \r and \t: one post is always one line. */

#include "store/store_status.h"

#include <stdbool.h>
#include <stddef.h>

#define INDIGO_CACHE_VERSION 1
#define INDIGO_CACHE_POSTS_MAX 40
#define INDIGO_CACHE_URI_MAX 160
#define INDIGO_CACHE_CID_MAX 80
#define INDIGO_CACHE_HANDLE_MAX 64
#define INDIGO_CACHE_NAME_MAX 64
/* Post text is held in a buffer this size, so stored text is at most
 * INDIGO_CACHE_TEXT_MAX - 1 bytes and never needs cutting. */
#define INDIGO_CACHE_TEXT_MAX 300

/* Worst case per post: the escaped name and text are twice their length, plus
 * the tabs, the "post" word, the newline and the terminator. The file limit is
 * derived from that, so a valid file always fits the store's buffer. */
#define INDIGO_CACHE_LINE_MAX                                                  \
    (sizeof "post\t\t\t\t\t\n" + INDIGO_CACHE_URI_MAX + INDIGO_CACHE_CID_MAX + \
     INDIGO_CACHE_HANDLE_MAX + 2 * INDIGO_CACHE_NAME_MAX + 2 * INDIGO_CACHE_TEXT_MAX)
#define INDIGO_CACHE_FILE_MAX \
    (sizeof "indigo-cache 1\n" + sizeof "end\n" + INDIGO_CACHE_POSTS_MAX * INDIGO_CACHE_LINE_MAX)

typedef struct {
    char uri[INDIGO_CACHE_URI_MAX];
    char cid[INDIGO_CACHE_CID_MAX];
    char handle[INDIGO_CACHE_HANDLE_MAX];
    char display_name[INDIGO_CACHE_NAME_MAX];
    char text[INDIGO_CACHE_TEXT_MAX];
} indigo_cache_post;

/* Large enough that callers should keep one in static storage, not on the
 * 3DS stack. */
typedef struct {
    size_t count;
    indigo_cache_post posts[INDIGO_CACHE_POSTS_MAX];
} indigo_cache_timeline;

void indigo_cache_timeline_clear(indigo_cache_timeline *t);

/* Writes at most `cap` bytes of the file. A post with an empty uri, or a uri,
 * cid or handle holding a tab or line break, is refused with
 * INDIGO_CODEC_INCOMPLETE rather than written half-formed. More than
 * INDIGO_CACHE_POSTS_MAX posts is INDIGO_CODEC_TOO_BIG. */
indigo_codec_status indigo_cache_encode(const indigo_cache_timeline *t, char *out,
                                        size_t cap, size_t *len);

/* Tolerates missing, empty, truncated and garbage input. Any failure leaves
 * `out` empty, so a caller that ignores the status shows nothing rather than a
 * partial timeline. */
indigo_codec_status indigo_cache_decode(const char *data, size_t len,
                                        indigo_cache_timeline *out);

#endif
