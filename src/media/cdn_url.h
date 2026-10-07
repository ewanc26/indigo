#ifndef INDIGO_MEDIA_CDN_URL_H
#define INDIGO_MEDIA_CDN_URL_H

/* The image URL Indigo actually fetches for a Bluesky CDN URL.
 *
 * The AppView's URLs name the full-size preset and no format, and the CDN
 * answers a URL with no format in WebP, which Wolfram's decoder cannot read. A
 * 3DS draws an avatar at well under 128 px, so the rewrite asks for the
 * 128 px avatar thumbnail and a JPEG (a couple of kilobytes), and for a post
 * image or link card the feed thumbnail as a JPEG. The rewrite itself is
 * Wolfram's (wolfram/cdn.h); this only picks the preset for each use. */

#include <stddef.h>

typedef enum {
    INDIGO_CDN_AVATAR,    /* a person's picture: the 128 px thumbnail */
    INDIGO_CDN_THUMBNAIL, /* a post image or link-card thumbnail */
} indigo_cdn_kind;

/* Writes the URL to fetch for `url` into `out` (`cap` bytes). A URL that is not
 * a Bluesky CDN URL is copied unchanged. A result that does not fit is an
 * empty string, never a truncated one: a URL cut short is a different URL, and
 * the CDN answers it with an error. NULL `url` gives an empty string. */
void indigo_media_cdn_url(char *out, size_t cap, const char *url, indigo_cdn_kind kind);

#endif
