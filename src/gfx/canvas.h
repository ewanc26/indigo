#ifndef INDIGO_CANVAS_H
#define INDIGO_CANVAS_H

#include <stdbool.h>
#include <stdint.h>

#define INDIGO_CANVAS_MAX_CMDS 128
#define INDIGO_CANVAS_TEXT_BYTES 4096
#define INDIGO_CANVAS_MAX_SPANS 32
/* Longest piece of one text command a backend has to copy out. */
#define INDIGO_CANVAS_SEGMENT_MAX 160

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
    uint16_t span_first;
    uint16_t span_count;
} indigo_cmd;

/* A byte range of a text command drawn in another colour (links, mentions). */
typedef struct {
    uint16_t start;
    uint16_t end;
    uint32_t color;
} indigo_span;

/* One run of a text command in a single colour, as the backends draw it. */
typedef struct {
    uint16_t start;
    uint16_t end;
    uint32_t color;
} indigo_segment;

/*
 * A fixed-size, allocation-free display list for one screen. Layout fills it;
 * the 3DS backend and the host snapshot renderer replay it.
 */
typedef struct {
    int width;
    int height;
    unsigned count;
    unsigned text_len;
    unsigned span_count;
    bool overflow;
    indigo_cmd cmds[INDIGO_CANVAS_MAX_CMDS];
    indigo_span spans[INDIGO_CANVAS_MAX_SPANS];
    char text[INDIGO_CANVAS_TEXT_BYTES];
} indigo_canvas;

void indigo_canvas_init(indigo_canvas *canvas, int width, int height);
bool indigo_canvas_rect(indigo_canvas *canvas, float x, float y, float w, float h,
                        uint32_t color);
bool indigo_canvas_text(indigo_canvas *canvas, float x, float y, float scale,
                        uint32_t color, const char *format, ...)
    __attribute__((format(printf, 6, 7)));
/* Recolour [start,end) of the most recently added text command. Spans must be
 * added in order and not overlap; false (and flagged overflow) when full. */
bool indigo_canvas_span(indigo_canvas *canvas, unsigned start, unsigned end,
                        uint32_t color);

/* Split a text command into contiguous single-colour runs covering all of it.
 * `out` must hold 2 * INDIGO_CANVAS_MAX_SPANS + 1 entries. */
unsigned indigo_canvas_segments(const indigo_canvas *canvas, const indigo_cmd *cmd,
                                indigo_segment *out);

const char *indigo_canvas_cmd_text(const indigo_canvas *canvas, const indigo_cmd *cmd);

#endif
