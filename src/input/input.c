#include "input/input.h"

#include <3ds.h>
#include <wolfram/drag.h>

/* How long a direction is held before it repeats, and the gap between repeats, in
 * frames at 60 Hz: quick enough to run down a feed, slow enough to stop on a post. */
#define REPEAT_DELAY 20
#define REPEAT_INTERVAL 5

static wf_drag s_drag;

void
indigo_input_init(indigo_input *input)
{
    *input = (indigo_input) {0};
    hidSetRepeatParameters(REPEAT_DELAY, REPEAT_INTERVAL);
}

void
indigo_input_begin_frame(indigo_input *input)
{
    input->pressed = hidKeysDown();
    input->released = hidKeysUp();

    input->confirm = (input->pressed & KEY_A) != 0;
    input->back = (input->pressed & KEY_B) != 0;
    input->quit = (input->pressed & KEY_START) != 0;
    /* The Circle Pad counts as the D-pad, and both repeat while held. */
    input->up = (hidKeysDownRepeat() & KEY_UP) != 0;
    input->down = (hidKeysDownRepeat() & KEY_DOWN) != 0;
    /* L and R, or the C-Stick on a New 3DS, a screenful at a time. */
    input->page_up = (input->pressed & (KEY_L | KEY_CSTICK_UP)) != 0;
    input->page_down = (input->pressed & (KEY_R | KEY_CSTICK_DOWN)) != 0;
    input->like = (input->pressed & KEY_Y) != 0;
    input->repost = (input->pressed & KEY_X) != 0;
    input->refresh = (input->pressed & KEY_SELECT) != 0;
    input->zr = (input->pressed & KEY_ZR) != 0;
}

void
indigo_input_poll(indigo_input *input)
{
    input->held = hidKeysHeld();

    circlePosition circle;
    hidCircleRead(&circle);
    input->circle_x = circle.dx;
    input->circle_y = circle.dy;

    circlePosition cstick;
    hidCstickRead(&cstick);
    input->cstick_x = cstick.dx;
    input->cstick_y = cstick.dy;

    touchPosition touch;
    hidTouchRead(&touch);

    input->touch_x = touch.px;
    input->touch_y = touch.py;
    input->touch_down = (input->held & KEY_TOUCH) != 0;
    input->touch_pressed = (input->pressed & KEY_TOUCH) != 0;
    input->touch_released = (input->released & KEY_TOUCH) != 0;

    input->drag_rows = 0;
    if (input->touch_pressed) {
        wf_drag_begin(&s_drag, input->touch_y);
        input->drag_start_x = input->touch_x;
        input->drag_start_y = input->touch_y;
    } else if (input->touch_down) {
        input->drag_rows = wf_drag_move(&s_drag, input->touch_y, INDIGO_LIST_ROW_PX);
    }
    if (input->touch_released) {
        (void) wf_drag_end(&s_drag);
    }
}

void
indigo_input_shutdown(indigo_input *input)
{
    (void) input;
}
