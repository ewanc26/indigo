#ifndef INDIGO_STORE_CACHE_CODEC_H
#define INDIGO_STORE_CACHE_CODEC_H

/* Cache codec: versioned line format for timeline/profile on-card cache.
 * Same rules as session_codec / settings_codec: no newlines in values,
 * version marker at top, corrupt file moved to .bad (see file.c).
 */

#include <stddef.h>
#include <stdbool.h>

/* Cache file format v1: each line is "key=value".
 * Required keys: VERSION=1, COUNT=N, then N post entries.
 * Post entry: POST_URI=<uri> POST_CID=<cid> POST_TEXT=<text> (truncated to fit).
 * No secrets written — feed data only.
 */

#define INDIGO_CACHE_VERSION 1

#endif
