#ifndef INDIGO_UI_H
#define INDIGO_UI_H

#include "app/app.h"
#include "input/input.h"

#include <stdbool.h>

bool indigo_ui_init(void);
void indigo_ui_draw(const indigo_app *app, const indigo_input *input);
void indigo_ui_shutdown(void);

#endif
