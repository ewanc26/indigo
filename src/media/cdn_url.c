#include "media/cdn_url.h"

#include <string.h>
#include <wolfram/cdn.h>

void
indigo_media_cdn_url(char *out, size_t cap, const char *url, indigo_cdn_kind kind)
{
    wf_cdn_preset preset = kind == INDIGO_CDN_AVATAR ? WF_CDN_AVATAR_THUMBNAIL
                                                     : WF_CDN_FEED_THUMBNAIL;

    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (!url || url[0] == '\0') {
        return;
    }
    if (wf_bsky_cdn_url(url, preset, WF_CDN_FORMAT_JPEG, out, cap) == WF_OK) {
        return;
    }
    out[0] = '\0';
    if (strlen(url) < cap) {
        memcpy(out, url, strlen(url) + 1);
    }
}
