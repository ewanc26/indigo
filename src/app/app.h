#ifndef INDIGO_APP_H
#define INDIGO_APP_H

#include <stdbool.h>

typedef struct indigo_input indigo_input;

typedef enum {
    INDIGO_SCREEN_HOME = 0,
    INDIGO_SCREEN_PROFILE,
    INDIGO_SCREEN_SEARCH,
} indigo_screen;

typedef struct {
    indigo_screen screen;
    bool quit_requested;
} indigo_app;

void indigo_app_init(indigo_app *app);
void indigo_app_update(indigo_app *app, const indigo_input *input);
void indigo_app_shutdown(indigo_app *app);
bool indigo_app_should_quit(const indigo_app *app);

#endif
