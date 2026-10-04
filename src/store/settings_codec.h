#ifndef INDIGO_SETTINGS_CODEC_H
#define INDIGO_SETTINGS_CODEC_H

#include "store/store_status.h"

#include <stdbool.h>
#include <stddef.h>

#define INDIGO_SETTINGS_FORMAT_VERSION 1
#define INDIGO_SETTINGS_FEED_MAX 128
#define INDIGO_SETTINGS_FILE_MAX 512

/* "Auto" is not a placeholder: the top screen is a backlight LCD on some
 * models and an OLED on others, so following the hardware has to be something
 * the app decides at runtime rather than a fixed palette choice. */
typedef enum {
    INDIGO_THEME_AUTO = 0,
    INDIGO_THEME_LIGHT = 1,
    INDIGO_THEME_DARK = 2,
} indigo_theme;

/* Percentages rather than pixel sizes: layout measures in pixels, and the
 * renderer has no font metric to scale against yet. */
typedef enum {
    INDIGO_TEXT_SCALE_SMALL = 100,
    INDIGO_TEXT_SCALE_NORMAL = 115,
    INDIGO_TEXT_SCALE_LARGE = 130,
} indigo_text_scale;

/* Accessibility preferences and one diagnostic switch. Nothing here is secret,
 * so unlike the saved session this struct needs no volatile scrub on the way
 * out. */
typedef struct {
    indigo_theme theme;
    indigo_text_scale text_scale;
    bool reduce_motion;
    bool high_contrast;
    bool large_targets;
    /* Show a post image's alt text under the picture. Off by default: alt text
     * is written for a screen reader, and most people looking at a photograph do
     * not want to read its description as well. */
    bool alt_text;
    bool diagnostics;
    /* AT URI or handle of the feed to open at startup. Empty means the
     * default home timeline. */
    char default_feed[INDIGO_SETTINGS_FEED_MAX];
} indigo_settings;

void indigo_settings_defaults(indigo_settings *out);

/* Versioned "key=value" lines. Refuses values containing newlines. */
indigo_codec_status indigo_settings_encode(const indigo_settings *s, char *out,
                                           size_t cap, size_t *len);

/* Tolerates missing, empty, truncated and garbage input.
 *
 * Unlike the session codec, a key that is absent or whose value will not parse
 * falls back to its default instead of failing the file, so one bad value
 * cannot cost the user the rest of their settings. A missing "end" marker is
 * still a corrupt file, and out remains usable at its defaults on every
 * non-OK return. */
indigo_codec_status indigo_settings_decode(const char *data, size_t len,
                                           indigo_settings *out);

/* Forces every field back into range. Called after decoding, and by encode, so
 * an out-of-range value on disk or in a caller's struct cannot reach layout. */
void indigo_settings_clamp(indigo_settings *s);

#endif
