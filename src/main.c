#include "app/app.h"
#include "atproto/atproto.h"
#include "input/input.h"
#include "ui/ui.h"
#include "util/log.h"

#include <3ds.h>

int
main(void)
{
    indigo_log_init();

    gfxInitDefault();

    if (!indigo_ui_init()) {
        indigo_log_error("could not initialise the 3DS renderer");
        gfxExit();
        indigo_log_shutdown();
        return 1;
    }

    indigo_atproto_init();

    indigo_input input;
    indigo_input_init(&input);

    indigo_app app;
    indigo_app_init(&app);

    while (aptMainLoop() && !indigo_app_should_quit(&app)) {
        hidScanInput();

        indigo_input_begin_frame(&input);
        indigo_input_poll(&input);

        indigo_app_update(&app, &input);
        indigo_ui_draw(&app, &input);

        gspWaitForVBlank();
    }

    indigo_app_shutdown(&app);
    indigo_input_shutdown(&input);
    indigo_atproto_shutdown();
    indigo_ui_shutdown();
    gfxExit();
    indigo_log_shutdown();

    return 0;
}
