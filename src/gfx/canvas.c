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

const char *
indigo_canvas_cmd_text(const indigo_canvas *canvas, const indigo_cmd *cmd)
{
    return canvas->text + cmd->text_offset;
}
