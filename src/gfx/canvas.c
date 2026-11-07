#include "gfx/canvas.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void
indigo_canvas_init(indigo_canvas *canvas, int width, int height)
{
    /* Every array is read through a count and every live entry is written
     * before the count advances, so only the counters need resetting. Clearing
     * the payload scrubbed 128 commands, 24 image URLs and 4 KiB of text on
     * both screens every frame, which was 22% of host layout samples. */
    canvas->width = width;
    canvas->height = height;
    canvas->count = 0;
    canvas->text_len = 0;
    canvas->span_count = 0;
    canvas->image_count = 0;
    canvas->overflow = false;
    canvas->qr_size = 0;
    canvas->text[0] = '\0';
}

static indigo_cmd *
indigo_canvas_push(indigo_canvas *canvas)
{
    if (canvas->count >= INDIGO_CANVAS_MAX_CMDS) {
        canvas->overflow = true;
        return NULL;
    }

    return &canvas->cmds[canvas->count++];
}

bool
indigo_canvas_rect(indigo_canvas *canvas, float x, float y, float w, float h,
                   uint32_t color)
{
    indigo_cmd *cmd = indigo_canvas_push(canvas);

    if (!cmd) {
        return false;
    }

    *cmd = (indigo_cmd) {
        .kind = INDIGO_CMD_RECT, .x = x, .y = y, .w = w, .h = h, .color = color,
    };
    return true;
}

bool
indigo_canvas_qr(indigo_canvas *canvas, float x, float y, unsigned module_px,
                 unsigned size, const uint8_t *modules)
{
    indigo_cmd *cmd;

    if (!modules || size == 0 || size > WF_QR_MAX_SIZE || module_px == 0 || canvas->qr_size != 0) {
        return false;
    }
    cmd = indigo_canvas_push(canvas);
    if (!cmd) {
        return false;
    }
    memcpy(canvas->qr, modules, size * size);
    canvas->qr_size = (uint8_t) size;
    *cmd = (indigo_cmd) {
        .kind = INDIGO_CMD_QR, .x = x, .y = y, .w = (float) module_px,
        .qr_size = (uint8_t) size,
    };
    return true;
}

/* The text buffer is a byte pool, so a copy is only legal when the terminator
 * also fits. A full buffer is an overflow, not a silent truncation. */
static bool
canvas_text_fits(indigo_canvas *canvas, size_t len)
{
    if (canvas->text_len >= INDIGO_CANVAS_TEXT_BYTES ||
        len >= INDIGO_CANVAS_TEXT_BYTES - canvas->text_len) {
        if (canvas->text_len < INDIGO_CANVAS_TEXT_BYTES) {
            canvas->text[canvas->text_len] = '\0';
        }
        canvas->overflow = true;
        return false;
    }
    return true;
}

static bool
canvas_text_commit(indigo_canvas *canvas, size_t len, float x, float y, float scale,
                   uint32_t color)
{
    indigo_cmd *cmd;

    canvas->text[canvas->text_len + len] = '\0';
    cmd = indigo_canvas_push(canvas);
    if (!cmd) {
        canvas->text[canvas->text_len] = '\0';
        return false;
    }
    *cmd = (indigo_cmd) {
        .kind = INDIGO_CMD_TEXT,
        .x = x,
        .y = y,
        .scale = scale,
        .color = color,
        .text_offset = (uint16_t) canvas->text_len,
    };
    canvas->text_len += (unsigned) len + 1;
    return true;
}

/* Copy three literal spans into the pool. The fast paths below all resolve to
 * this, so a label that needs no printf conversion never enters libc's
 * formatter. */
static bool
canvas_text_parts(indigo_canvas *canvas, float x, float y, float scale, uint32_t color,
                  const char *prefix, size_t prefix_len, const char *value,
                  size_t value_len, const char *suffix, size_t suffix_len)
{
    size_t len = prefix_len + value_len + suffix_len;
    char *dst;

    if (!canvas_text_fits(canvas, len)) {
        return false;
    }
    dst = canvas->text + canvas->text_len;
    memcpy(dst, prefix, prefix_len);
    memcpy(dst + prefix_len, value, value_len);
    memcpy(dst + prefix_len + value_len, suffix, suffix_len);
    return canvas_text_commit(canvas, len, x, y, scale, color);
}

bool
indigo_canvas_text(indigo_canvas *canvas, float x, float y, float scale,
                   uint32_t color, const char *format, ...)
{
    const char *percent;
    va_list args;

    if (canvas->count >= INDIGO_CANVAS_MAX_CMDS) {
        canvas->overflow = true;
        return false;
    }

    percent = strchr(format, '%');
    if (!percent) {
        return canvas_text_parts(canvas, x, y, scale, color, format, strlen(format),
                                 "", 0, "", 0);
    }

    /* A single %s with literal text either side: "%s", "A  %s", and the
     * video label are the common labels that were paying for __vfprintf. */
    if (percent[1] == 's' && strchr(percent + 2, '%') == NULL) {
        const char *value;

        va_start(args, format);
        value = va_arg(args, const char *);
        va_end(args);
        if (!value) {
            value = "(null)";
        }
        return canvas_text_parts(canvas, x, y, scale, color, format,
                                 (size_t) (percent - format), value, strlen(value),
                                 percent + 2, strlen(percent + 2));
    }

    /* Wrapped lines use this exact shape: precision bytes of the source, then
     * an optional ellipsis. */
    if (strcmp(format, "%.*s%s") == 0) {
        int precision;
        const char *left;
        const char *right;
        size_t left_len = 0;

        va_start(args, format);
        precision = va_arg(args, int);
        left = va_arg(args, const char *);
        right = va_arg(args, const char *);
        va_end(args);
        if (precision >= 0 && left && right) {
            while (left_len < (size_t) precision && left[left_len]) {
                left_len++;
            }
            return canvas_text_parts(canvas, x, y, scale, color, left, left_len,
                                     right, strlen(right), "", 0);
        }
    }

    if (!canvas_text_fits(canvas, 0)) {
        return false;
    }
    size_t room = INDIGO_CANVAS_TEXT_BYTES - canvas->text_len;
    va_start(args, format);
    int written = vsnprintf(canvas->text + canvas->text_len, room, format, args);
    va_end(args);

    if (written < 0 || (size_t) written >= room) {
        canvas->text[canvas->text_len] = '\0';
        canvas->overflow = true;
        return false;
    }

    return canvas_text_commit(canvas, (size_t) written, x, y, scale, color);
}

bool
indigo_canvas_span(indigo_canvas *canvas, unsigned start, unsigned end, uint32_t color)
{
    indigo_cmd *cmd;

    if (canvas->count == 0 || canvas->cmds[canvas->count - 1].kind != INDIGO_CMD_TEXT ||
        start >= end) {
        return false;
    }
    if (canvas->span_count >= INDIGO_CANVAS_MAX_SPANS) {
        canvas->overflow = true;
        return false;
    }
    cmd = &canvas->cmds[canvas->count - 1];
    if (cmd->span_count == 0) {
        cmd->span_first = (uint16_t) canvas->span_count;
    }
    canvas->spans[canvas->span_count++] = (indigo_span) {
        .start = (uint16_t) start, .end = (uint16_t) end, .color = color,
    };
    cmd->span_count++;
    return true;
}

bool
indigo_canvas_image(indigo_canvas *canvas, float x, float y, float w, float h,
                    const char *url, uint32_t placeholder)
{
    indigo_cmd *cmd;
    size_t len;
    unsigned index = 0;
    bool known = false;

    if (!url || (len = strlen(url)) == 0 || len >= INDIGO_CANVAS_IMAGE_URL_MAX) {
        canvas->overflow = true;
        return false;
    }
    for (unsigned i = 0; i < canvas->image_count; i++) {
        if (strcmp(canvas->images[i].url, url) == 0) {
            index = i;
            known = true;
            break;
        }
    }
    if (!known) {
        if (canvas->image_count >= INDIGO_CANVAS_MAX_IMAGES) {
            canvas->overflow = true;
            return false;
        }
        index = canvas->image_count++;
        memcpy(canvas->images[index].url, url, len + 1);
        canvas->images[index].placeholder = placeholder;
    }
    cmd = indigo_canvas_push(canvas);

    if (!cmd) {
        /* The command is what makes this a failure; an unused image entry is
         * harmless, but the overflow flag is already set. */
        return false;
    }
    *cmd = (indigo_cmd) {
        .kind = INDIGO_CMD_IMAGE,
        .x = x,
        .y = y,
        .w = w,
        .h = h,
        .color = placeholder,
        .image_index = (uint16_t) index,
    };
    return true;
}

unsigned
indigo_canvas_segments(const indigo_canvas *canvas, const indigo_cmd *cmd, indigo_segment *out)
{
    unsigned len = (unsigned) strlen(canvas->text + cmd->text_offset);
    unsigned pos = 0;
    unsigned n = 0;

    for (unsigned i = 0; i < cmd->span_count; i++) {
        const indigo_span *s = &canvas->spans[cmd->span_first + i];
        unsigned end = s->end > len ? len : s->end;

        if (s->start < pos || s->start >= end) {
            continue;
        }
        if (s->start > pos) {
            out[n++] = (indigo_segment) {(uint16_t) pos, s->start, cmd->color};
        }
        out[n++] = (indigo_segment) {s->start, (uint16_t) end, s->color};
        pos = end;
    }
    if (pos < len || n == 0) {
        out[n++] = (indigo_segment) {(uint16_t) pos, (uint16_t) len, cmd->color};
    }
    return n;
}

const char *
indigo_canvas_cmd_text(const indigo_canvas *canvas, const indigo_cmd *cmd)
{
    return canvas->text + cmd->text_offset;
}
