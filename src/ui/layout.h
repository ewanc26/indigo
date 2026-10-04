#ifndef INDIGO_LAYOUT_H
#define INDIGO_LAYOUT_H

#include "app/app.h"
#include "gfx/canvas.h"
#include "input/input.h"

#include <stdbool.h>

#define INDIGO_TOP_WIDTH 400
#define INDIGO_TOP_HEIGHT 240
#define INDIGO_BOTTOM_WIDTH 320
#define INDIGO_BOTTOM_HEIGHT 240

typedef enum {
    INDIGO_ACTION_NONE = 0,
    INDIGO_ACTION_BACK,
    INDIGO_ACTION_FIELD_SERVICE,
    INDIGO_ACTION_FIELD_HANDLE,
    INDIGO_ACTION_FIELD_PASSWORD,
    INDIGO_ACTION_SIGN_IN,
    INDIGO_ACTION_SIGN_OUT,
    INDIGO_ACTION_ROW0,
    INDIGO_ACTION_ROW1,
    INDIGO_ACTION_ROW2,
    INDIGO_ACTION_LIKE,
    INDIGO_ACTION_REPOST,
    INDIGO_ACTION_REFRESH,
    INDIGO_ACTION_OPEN,
    INDIGO_ACTION_REPLY,
    INDIGO_ACTION_AUTHOR,
    INDIGO_ACTION_MENU,
    INDIGO_ACTION_MENU0,
    INDIGO_ACTION_MENU1,
    INDIGO_ACTION_MENU2,
    INDIGO_ACTION_MENU3,
    INDIGO_ACTION_MENU4,
    INDIGO_ACTION_EDIT,
    INDIGO_ACTION_FIELD_QUERY,
    INDIGO_ACTION_TOGGLE,
    INDIGO_ACTION_SEND,
    INDIGO_ACTION_FOLLOW,
    INDIGO_ACTION_MUTE,
    INDIGO_ACTION_BLOCK,
    INDIGO_ACTION_FOLLOWERS,
    INDIGO_ACTION_FOLLOWING,
    INDIGO_ACTION_POSTS,
    INDIGO_ACTION_PINNED,
    INDIGO_ACTION_SETTINGS_ROW0,
    INDIGO_ACTION_SETTINGS_ROW1,
    INDIGO_ACTION_SETTINGS_ROW2,
    INDIGO_ACTION_SETTINGS_ROW3,
    INDIGO_ACTION_SETTINGS_ROW4,
    INDIGO_ACTION_SETTINGS_ROW5,
    INDIGO_ACTION_SETTINGS_ROW6,
} indigo_action;

/* Posts visible at once in the bottom list. */
#define INDIGO_TIMELINE_ROWS 3

typedef struct {
    float x;
    float y;
    float w;
    float h;
} indigo_rect;

/* Touch targets on the bottom screen, shared by drawing and hit testing. */
indigo_rect indigo_layout_button_rect(indigo_action action);
/* The hit test reads two things out of the app -- which screen, and whether
 * touch targets are enlarged -- so it takes those two rather than the app.
 * Building a 370KB indigo_app on the stack to read one field was the single
 * largest stack frame in the program, in a function called per touch event and
 * per target in the layout tests. */
indigo_action indigo_layout_hit_settings(indigo_screen screen, bool large_targets,
                                         int touch_x, int touch_y);
indigo_action indigo_layout_hit(indigo_screen screen, int touch_x, int touch_y);
indigo_action indigo_layout_hit_app(const indigo_app *app, int touch_x, int touch_y);

/* Pure layout: fills both display lists, touches no platform API. */
void indigo_layout_build(const indigo_app *app, const indigo_input *input,
                         indigo_canvas *top, indigo_canvas *bottom);

#endif
