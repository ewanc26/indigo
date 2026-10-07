#ifndef INDIGO_CANVAS_H
#define INDIGO_CANVAS_H

#include <stdbool.h>
#include <stdint.h>

#define INDIGO_CANVAS_MAX_CMDS 128
#define INDIGO_CANVAS_TEXT_BYTES 4096
#define INDIGO_CANVAS_MAX_SPANS 32
/* Longest piece of one text command a backend has to copy out. */
#define INDIGO_CANVAS_SEGMENT_MAX 160
/* Distinct images one screen may name. A URL rather than pixels, because
 * layout is pure: it says what should be on screen and the backend decides
 * whether the decoded pixels are there yet. Deduped by URL, so the same avatar
 * on three rows costs one entry. */
#define INDIGO_CANVAS_MAX_IMAGES 24
#define INDIGO_CANVAS_IMAGE_URL_MAX 160

/* Colours are packed 0xRRGGBBAA, independent of any backend. */
#define INDIGO_RGBA(r, g, b, a) \
    (((uint32_t) (r) << 24) | ((uint32_t) (g) << 16) | ((uint32_t) (b) << 8) | (uint32_t) (a))

typedef enum {
    INDIGO_CMD_RECT = 0,
    INDIGO_CMD_TEXT,
    INDIGO_CMD_IMAGE,
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
    /* Index into indigo_canvas.images; only meaningful for INDIGO_CMD_IMAGE. */
    uint16_t image_index;
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

/* An image a command refers to: which one to draw, and what to draw in its
 * place when it is not decoded yet. The placeholder colour is stored rather
 * than derived so both backends agree without either owning the media cache. */
typedef struct {
    char url[INDIGO_CANVAS_IMAGE_URL_MAX];
    uint32_t placeholder;
} indigo_canvas_image_ref;

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
    unsigned image_count;
    bool overflow;
    indigo_cmd cmds[INDIGO_CANVAS_MAX_CMDS];
    indigo_span spans[INDIGO_CANVAS_MAX_SPANS];
    indigo_canvas_image_ref images[INDIGO_CANVAS_MAX_IMAGES];
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

/* Draws the image at `url`, or `placeholder` in its box while nothing decoded
 * is available for it. Repeat calls with the same URL share one entry, so a
 * screen naming the same avatar on several rows still fits. False (and flagged
 * overflow) when the URL is too long for the cache to hold or the screen has
 * run out of image entries. */
bool indigo_canvas_image(indigo_canvas *canvas, float x, float y, float w,
                         float h, const char *url, uint32_t placeholder);

/* Split a text command into contiguous single-colour runs covering all of it.
 * `out` must hold 2 * INDIGO_CANVAS_MAX_SPANS + 1 entries. */
unsigned indigo_canvas_segments(const indigo_canvas *canvas, const indigo_cmd *cmd,
                                indigo_segment *out);

const char *indigo_canvas_cmd_text(const indigo_canvas *canvas, const indigo_cmd *cmd);

#endif
