#ifndef INDIGO_LAYOUT_H
#define INDIGO_LAYOUT_H

#include "app/app.h"
#include "gfx/canvas.h"
#include "input/input.h"

#define INDIGO_TOP_WIDTH 400
#define INDIGO_TOP_HEIGHT 240
#define INDIGO_BOTTOM_WIDTH 320
#define INDIGO_BOTTOM_HEIGHT 240

typedef enum {
    INDIGO_ACTION_NONE = 0,
    INDIGO_ACTION_PROFILE,
    INDIGO_ACTION_HOME,
} indigo_action;

typedef struct {
    float x;
    float y;
    float w;
    float h;
} indigo_rect;

/* Touch targets on the bottom screen, shared by drawing and hit testing. */
indigo_rect indigo_layout_button_rect(indigo_action action);
indigo_action indigo_layout_hit(int touch_x, int touch_y);

/* Pure layout: fills both display lists, touches no platform API. */
void indigo_layout_build(const indigo_app *app, const indigo_input *input,
                         indigo_canvas *top, indigo_canvas *bottom);

#endif
