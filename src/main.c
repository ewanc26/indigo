#include "app/app.h"
#include "input/input.h"
#include "net/net.h"
#include "ui/ui.h"
#include "util/log.h"

#include <3ds.h>

#include <stdbool.h>
#include <stdio.h>

int
main(void)
{
    indigo_log_init();

    if (!indigo_net_init()) {
        indigo_log_warn("network initialisation unavailable");
    }

    gfxInitDefault();

    PrintConsole top;
    PrintConsole bottom;
    consoleInit(GFX_TOP, &top);
    consoleInit(GFX_BOTTOM, &bottom);

    indigo_ui_init(&top, &bottom);
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

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    indigo_app_shutdown(&app);
    indigo_input_shutdown(&input);
    indigo_ui_shutdown();
    gfxExit();
    indigo_net_shutdown();
    indigo_log_shutdown();

    return 0;
}
