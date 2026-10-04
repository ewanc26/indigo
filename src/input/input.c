#include "input/input.h"

#include <3ds.h>

void
indigo_input_init(indigo_input *input)
{
    *input = (indigo_input) {0};
}

void
indigo_input_begin_frame(indigo_input *input)
{
    input->pressed = hidKeysDown();
    input->released = hidKeysUp();

    input->confirm = (input->pressed & KEY_A) != 0;
    input->back = (input->pressed & KEY_B) != 0;
    input->quit = (input->pressed & KEY_START) != 0;
    input->up = (input->pressed & KEY_DUP) != 0;
    input->down = (input->pressed & KEY_DDOWN) != 0;
    input->page_up = (input->pressed & KEY_L) != 0;
    input->page_down = (input->pressed & KEY_R) != 0;
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
}

void
indigo_input_shutdown(indigo_input *input)
{
    (void) input;
}
