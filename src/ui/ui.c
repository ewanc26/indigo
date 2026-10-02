#include "ui/ui.h"

#include <stdio.h>

static PrintConsole *s_top;
static PrintConsole *s_bottom;

void
indigo_ui_init(PrintConsole *top, PrintConsole *bottom)
{
    s_top = top;
    s_bottom = bottom;
}

void
indigo_ui_draw(const indigo_app *app, const indigo_input *input)
{
    consoleSelect(s_top);
    printf("\x1b[2J");
    printf("Indigo 0.1.0\n");
    printf("\n");
    printf("Native AT Protocol / Bluesky client\n");
    printf("for Nintendo 3DS\n");
    printf("\n");
    printf("Screen: %d\n", (int) app->screen);
    printf("\n");
    printf("A  Open profile\n");
    printf("B  Return home\n");
    printf("START  Quit\n");

    consoleSelect(s_bottom);
    printf("\x1b[2J");
    printf("Indigo scaffold\n");
    printf("\n");
    printf("Touch: %d, %d\n", input->touch_x, input->touch_y);
    printf("Touching: %s\n", input->touch_down ? "yes" : "no");
    printf("\n");
    printf("The UI layer is intentionally minimal.\n");
    printf("Native 3DS rendering comes before\n");
    printf("the Bluesky application layer.\n");
}

void
indigo_ui_shutdown(void)
{
    s_top = NULL;
    s_bottom = NULL;
}
