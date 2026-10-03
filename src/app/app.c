#include "app/app.h"
#include "input/input.h"
#include "ui/layout.h"

void
indigo_app_init(indigo_app *app)
{
    *app = (indigo_app) {
        .screen = INDIGO_SCREEN_HOME,
        .quit_requested = false,
    };
}

void
indigo_app_update(indigo_app *app, const indigo_input *input)
{
    if (input->quit) {
        app->quit_requested = true;
    }

    if (input->confirm) {
        app->screen = INDIGO_SCREEN_PROFILE;
    }

    if (input->back) {
        app->screen = INDIGO_SCREEN_HOME;
    }

    if (input->touch_pressed) {
        switch (indigo_layout_hit(input->touch_x, input->touch_y)) {
        case INDIGO_ACTION_PROFILE:
            app->screen = INDIGO_SCREEN_PROFILE;
            break;
        case INDIGO_ACTION_HOME:
            app->screen = INDIGO_SCREEN_HOME;
            break;
        case INDIGO_ACTION_NONE:
            break;
        }
    }
}

void
indigo_app_shutdown(indigo_app *app)
{
    (void) app;
}

bool
indigo_app_should_quit(const indigo_app *app)
{
    return app->quit_requested;
}
