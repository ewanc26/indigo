#include "gfx/canvas.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void
indigo_canvas_init(indigo_canvas *canvas, int width, int height)
{
    memset(canvas, 0, sizeof(*canvas));
    canvas->width = width;
    canvas->height = height;
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
indigo_canvas_text(indigo_canvas *canvas, float x, float y, float scale,
                   uint32_t color, const char *format, ...)
{
    if (canvas->count >= INDIGO_CANVAS_MAX_CMDS) {
        canvas->overflow = true;
        return false;
    }

    size_t room = INDIGO_CANVAS_TEXT_BYTES - canvas->text_len;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(canvas->text + canvas->text_len, room, format, args);
    va_end(args);

    if (written < 0 || (size_t) written >= room) {
        canvas->text[canvas->text_len] = '\0';
        canvas->overflow = true;
        return false;
    }

    indigo_cmd *cmd = indigo_canvas_push(canvas);

    *cmd = (indigo_cmd) {
        .kind = INDIGO_CMD_TEXT,
        .x = x,
        .y = y,
        .scale = scale,
        .color = color,
        .text_offset = (uint16_t) canvas->text_len,
    };
    canvas->text_len += (unsigned) written + 1;
    return true;
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
