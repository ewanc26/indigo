#ifndef INDIGO_UI_H
#define INDIGO_UI_H

#include "app/app.h"
#include "input/input.h"

#include <stdbool.h>

bool indigo_ui_init(void);
void indigo_ui_draw(const indigo_app *app, const indigo_input *input);
void indigo_ui_shutdown(void);

/* Forget every decoded image and free the GPU textures they uploaded. Called
 * when a session ends, so the next account starts from an empty cache rather
 * than one holding the previous account's faces. */
void indigo_ui_clear_images(void);

#endif
