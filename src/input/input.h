#ifndef INDIGO_INPUT_H
#define INDIGO_INPUT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct indigo_input {
    uint32_t held;
    uint32_t pressed;
    uint32_t released;

    int16_t circle_x;
    int16_t circle_y;
    int16_t cstick_x;
    int16_t cstick_y;

    int touch_x;
    int touch_y;
    bool touch_down;
    bool touch_pressed;
    bool touch_released;

    /* Timeline controls, one abstraction over the physical buttons. */
    bool up;
    bool down;
    bool page_up;
    bool page_down;
    bool like;
    bool repost;
    bool refresh;

    bool confirm;
    bool back;
    bool quit;
} indigo_input;

void indigo_input_init(indigo_input *input);
void indigo_input_begin_frame(indigo_input *input);
void indigo_input_poll(indigo_input *input);
void indigo_input_shutdown(indigo_input *input);

#endif
