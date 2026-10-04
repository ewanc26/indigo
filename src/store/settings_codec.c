#include "store/settings_codec.h"

#include <stdio.h>
#include <string.h>

static bool
has_break(const char *v)
{
    return strchr(v, '\n') != NULL || strchr(v, '\r') != NULL;
}

void
indigo_settings_defaults(indigo_settings *out)
{
    memset(out, 0, sizeof *out);
    out->theme = INDIGO_THEME_AUTO;
    out->text_scale = INDIGO_TEXT_SCALE_NORMAL;
    out->reduce_motion = false;
    out->high_contrast = false;
    out->large_targets = false;
    out->alt_text = false;
    /* On by default, matching the behaviour from before settings existed. A
     * log that stops being written because a new file appeared would be a
     * regression nobody asked for. */
    out->diagnostics = true;
}

void
indigo_settings_clamp(indigo_settings *s)
{
    /* Compared as int on purpose. An all-non-negative enum gets an unsigned
     * underlying type, so testing `< INDIGO_THEME_AUTO` directly both trips
     * -Wtype-limits at -O2 and hides the wrapped-value case it exists for. */
    int theme = (int) s->theme;

    if (theme < (int) INDIGO_THEME_AUTO || theme > (int) INDIGO_THEME_DARK) {
        s->theme = INDIGO_THEME_AUTO;
    }
    switch (s->text_scale) {
    case INDIGO_TEXT_SCALE_SMALL:
    case INDIGO_TEXT_SCALE_NORMAL:
    case INDIGO_TEXT_SCALE_LARGE:
        break;
    default:
        s->text_scale = INDIGO_TEXT_SCALE_NORMAL;
        break;
    }
    s->default_feed[INDIGO_SETTINGS_FEED_MAX - 1] = '\0';
}

indigo_codec_status
indigo_settings_encode(const indigo_settings *s, char *out, size_t cap,
                       size_t *len)
{
    indigo_settings c;

    if (has_break(s->default_feed)) {
        return INDIGO_CODEC_CORRUPT;
    }
    /* Encode what is on the way in, not what the caller meant: a struct filled
     * in without clamp() must still produce a file this decoder accepts. */
    c = *s;
    c.default_feed[INDIGO_SETTINGS_FEED_MAX - 1] = '\0';
    indigo_settings_clamp(&c);

    int n = snprintf(out, cap,
                     "indigo-settings %d\n"
                     "theme=%d\n"
                     "text_scale=%d\n"
                     "reduce_motion=%d\n"
                     "high_contrast=%d\n"
                     "large_targets=%d\n"
                     "alt_text=%d\n"
                     "diagnostics=%d\n"
                     "default_feed=%s\n"
                     "end\n",
                     INDIGO_SETTINGS_FORMAT_VERSION, (int) c.theme,
                     (int) c.text_scale, c.reduce_motion ? 1 : 0,
                     c.high_contrast ? 1 : 0, c.large_targets ? 1 : 0,
                     c.alt_text ? 1 : 0, c.diagnostics ? 1 : 0, c.default_feed);

    if (n < 0 || (size_t) n >= cap) {
        return INDIGO_CODEC_TOO_BIG;
    }
    if (len) {
        *len = (size_t) n;
    }
    return INDIGO_CODEC_OK;
}

/* Accepts a bounded run of decimal digits only: no sign, no whitespace, no
 * trailing junk, so " 1" or "1x" is rejected the same as "999". */
static bool
parse_number(const char *v, size_t n, int lo, int hi, int *out)
{
    int acc = 0;

    if (n == 0 || n > 3) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (v[i] < '0' || v[i] > '9') {
            return false;
        }
        acc = acc * 10 + (v[i] - '0');
    }
    if (acc < lo || acc > hi) {
        return false;
    }
    *out = acc;
    return true;
}

static bool
parse_bool(const char *v, size_t n, bool *out)
{
    if (n != 1) {
        return false;
    }
    if (v[0] == '1') {
        *out = true;
        return true;
    }
    if (v[0] == '0') {
        *out = false;
        return true;
    }
    return false;
}

indigo_codec_status
indigo_settings_decode(const char *data, size_t len, indigo_settings *out)
{
    static const char header[] = "indigo-settings ";
    const char *p = data;
    const char *end = data + len;
    bool saw_end = false;
    indigo_settings tmp;

    indigo_settings_defaults(out);
    if (len == 0) {
        return INDIGO_CODEC_EMPTY;
    }
    if (len > INDIGO_SETTINGS_FILE_MAX) {
        return INDIGO_CODEC_TOO_BIG;
    }
    if (len < sizeof header - 1 || memcmp(p, header, sizeof header - 1) != 0) {
        return INDIGO_CODEC_CORRUPT;
    }
    p += sizeof header - 1;
    {
        const char *nl = memchr(p, '\n', (size_t) (end - p));
        size_t vlen;

        if (!nl) {
            return INDIGO_CODEC_CORRUPT;
        }
        vlen = (size_t) (nl - p);
        if (vlen > 0 && p[vlen - 1] == '\r') {
            vlen--; /* same CRLF tolerance as the key lines below */
        }
        if (vlen != 1 || *p != '0' + INDIGO_SETTINGS_FORMAT_VERSION) {
            return INDIGO_CODEC_BAD_VERSION;
        }
        p = nl + 1;
    }

    /* Parsed into a local and committed only once the file is known good, so a
     * file that turns out to be truncated cannot leave the caller holding half
     * of one setting and the default for the rest. */
    tmp = *out;

    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t) (end - p));
        size_t line_len;

        if (!nl) {
            break; /* truncated final line */
        }
        line_len = (size_t) (nl - p);
        if (line_len > 0 && p[line_len - 1] == '\r') {
            line_len--; /* tolerate a file last edited with CRLF endings */
        }
        if (line_len == 3 && memcmp(p, "end", 3) == 0) {
            saw_end = true;
            break;
        }

        const char *eq = memchr(p, '=', line_len);

        if (!eq) {
            return INDIGO_CODEC_CORRUPT;
        }

        size_t klen = (size_t) (eq - p);
        const char *v = eq + 1;
        size_t vlen = line_len - klen - 1;
        int num = 0;
        bool flag = false;

        /* Every branch below ignores a value it cannot use and leaves the
         * default in place. Anything unrecognised is skipped, so a file
         * written by a newer Indigo still loads. */
        if (klen == 5 && memcmp(p, "theme", 5) == 0) {
            if (parse_number(v, vlen, (int) INDIGO_THEME_AUTO,
                             (int) INDIGO_THEME_DARK, &num)) {
                tmp.theme = (indigo_theme) num;
            }
        } else if (klen == 10 && memcmp(p, "text_scale", 10) == 0) {
            /* A range check alone is not enough: 101 is inside 100..130 but
             * is not a scale the renderer knows how to apply. */
            if (parse_number(v, vlen, (int) INDIGO_TEXT_SCALE_SMALL,
                             (int) INDIGO_TEXT_SCALE_LARGE, &num) &&
                (num == (int) INDIGO_TEXT_SCALE_SMALL ||
                 num == (int) INDIGO_TEXT_SCALE_NORMAL ||
                 num == (int) INDIGO_TEXT_SCALE_LARGE)) {
                tmp.text_scale = (indigo_text_scale) num;
            }
        } else if (klen == 13 && memcmp(p, "reduce_motion", 13) == 0) {
            if (parse_bool(v, vlen, &flag)) {
                tmp.reduce_motion = flag;
            }
        } else if (klen == 8 && memcmp(p, "alt_text", 8) == 0) {
            if (parse_bool(v, vlen, &flag)) {
                tmp.alt_text = flag;
            }
        } else if (klen == 13 && memcmp(p, "high_contrast", 13) == 0) {
            if (parse_bool(v, vlen, &flag)) {
                tmp.high_contrast = flag;
            }
        } else if (klen == 13 && memcmp(p, "large_targets", 13) == 0) {
            if (parse_bool(v, vlen, &flag)) {
                tmp.large_targets = flag;
            }
        } else if (klen == 11 && memcmp(p, "diagnostics", 11) == 0) {
            if (parse_bool(v, vlen, &flag)) {
                tmp.diagnostics = flag;
            }
        } else if (klen == 12 && memcmp(p, "default_feed", 12) == 0) {
            if (vlen < sizeof tmp.default_feed) {
                memcpy(tmp.default_feed, v, vlen);
                tmp.default_feed[vlen] = '\0';
            }
        }
        p = nl + 1;
    }

    if (!saw_end) {
        return INDIGO_CODEC_CORRUPT;
    }
    indigo_settings_clamp(&tmp);
    *out = tmp;
    return INDIGO_CODEC_OK;
}
