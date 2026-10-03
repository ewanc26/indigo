#ifndef INDIGO_CANVAS_H
#define INDIGO_CANVAS_H

#include <stdbool.h>
#include <stdint.h>

#define INDIGO_CANVAS_MAX_CMDS 96
#define INDIGO_CANVAS_TEXT_BYTES 2048

/* Colours are packed 0xRRGGBBAA, independent of any backend. */
#define INDIGO_RGBA(r, g, b, a) \
    (((uint32_t) (r) << 24) | ((uint32_t) (g) << 16) | ((uint32_t) (b) << 8) | (uint32_t) (a))

typedef enum {
    INDIGO_CMD_RECT = 0,
    INDIGO_CMD_TEXT,
} indigo_cmd_kind;

typedef struct {
    indigo_cmd_kind kind;
    float x;
    float y;
    float w;
    float h;
    float scale;
    uint32_t color;
    uint16_t text_offset;
} indigo_cmd;

/*
 * A fixed-size, allocation-free display list for one screen. Layout fills it;
 * the 3DS backend and the host snapshot renderer replay it.
 */
typedef struct {
    int width;
    int height;
    unsigned count;
    unsigned text_len;
    bool overflow;
    indigo_cmd cmds[INDIGO_CANVAS_MAX_CMDS];
    char text[INDIGO_CANVAS_TEXT_BYTES];
} indigo_canvas;

void indigo_canvas_init(indigo_canvas *canvas, int width, int height);
bool indigo_canvas_rect(indigo_canvas *canvas, float x, float y, float w, float h,
                        uint32_t color);
bool indigo_canvas_text(indigo_canvas *canvas, float x, float y, float scale,
                        uint32_t color, const char *format, ...)
    __attribute__((format(printf, 6, 7)));
const char *indigo_canvas_cmd_text(const indigo_canvas *canvas, const indigo_cmd *cmd);

#endif
