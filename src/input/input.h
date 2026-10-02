#ifndef INDIGO_INPUT_H
#define INDIGO_INPUT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct indigo_input {
    uint32_t held;
    uint32_t pressed;
    int touch_x;
    int touch_y;
    bool touch_down;
    bool touch_pressed;
    bool confirm;
    bool back;
    bool quit;
} indigo_input;

void indigo_input_init(indigo_input *input);
void indigo_input_begin_frame(indigo_input *input);
void indigo_input_poll(indigo_input *input);
void indigo_input_shutdown(indigo_input *input);

#endif
