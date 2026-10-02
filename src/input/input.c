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
    input->confirm = (input->pressed & KEY_A) != 0;
    input->back = (input->pressed & KEY_B) != 0;
    input->quit = (input->pressed & KEY_START) != 0;
    input->touch_pressed = false;
}

void
indigo_input_poll(indigo_input *input)
{
    input->held = hidKeysHeld();

    touchPosition touch;
    hidTouchRead(&touch);

    input->touch_x = touch.px;
    input->touch_y = touch.py;
    input->touch_down = (input->held & KEY_TOUCH) != 0;
    input->touch_pressed = input->touch_down &&
                          (input->pressed & KEY_TOUCH) != 0;
}

void
indigo_input_shutdown(indigo_input *input)
{
    (void) input;
}
