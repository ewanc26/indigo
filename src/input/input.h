#ifndef INDIGO_INPUT_H
#define INDIGO_INPUT_H

#include <stdbool.h>
#include <stdint.h>

/* The height of one list row on the touch screen, row to row. A drag of this many
 * pixels scrolls one row; the layout draws its rows at this pitch. */
#define INDIGO_LIST_ROW_PX 50

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

    /* Dragging a finger along a list. `drag_rows` is how many whole rows the drag
     * has moved this frame (positive: the finger went up, later rows come into
     * view; 0 while the touch is a tap), and the start is where it landed, so a
     * screen can tell whether it began on its list. */
    int drag_rows;
    int drag_start_x;
    int drag_start_y;

    /* Timeline controls, one abstraction over the physical buttons. up and down
     * repeat while held, and the Circle Pad counts as the D-pad. */
    bool up;
    bool down;
    bool page_up;
    bool page_down;
    bool like;
    bool repost;
    bool refresh;
    /* ZR, which only exists on a New 3DS. An enhancement rather than a
     * requirement: everything it does is also on a touch target, so an old
     * console loses a shortcut rather than a control. */
    bool zr;

    bool confirm;
    bool back;
    bool quit;
} indigo_input;

void indigo_input_init(indigo_input *input);
void indigo_input_begin_frame(indigo_input *input);
void indigo_input_poll(indigo_input *input);
void indigo_input_shutdown(indigo_input *input);

#endif
