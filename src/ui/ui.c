#include "ui/ui.h"

#include <citro2d.h>

#include <stdio.h>

#define TOP_WIDTH 400.0f
#define TOP_HEIGHT 240.0f
#define BOTTOM_WIDTH 320.0f
#define BOTTOM_HEIGHT 240.0f

static C3D_RenderTarget *s_top;
static C3D_RenderTarget *s_bottom;
static C2D_TextBuf s_text_buf;

static bool
indigo_ui_text(C2D_Text *text, const char *string)
{
    if (!s_text_buf) {
        return false;
    }

    C2D_TextBufClear(s_text_buf);
    C2D_TextParse(text, s_text_buf, string);
    C2D_TextOptimize(text);
    return true;
}

bool
indigo_ui_init(void)
{
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        return false;
    }

    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        C3D_Fini();
        return false;
    }

    C2D_Prepare();

    s_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    s_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    s_text_buf = C2D_TextBufNew(1024);

    if (!s_top || !s_bottom || !s_text_buf) {
        indigo_ui_shutdown();
        return false;
    }

    return true;
}

void
indigo_ui_draw(const indigo_app *app, const indigo_input *input)
{
    C2D_Text title;
    C2D_Text subtitle;
    C2D_Text controls;
    C2D_Text touch;

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    C2D_TargetClear(s_top, C2D_Color32(18, 20, 26, 255));
    C2D_SceneBegin(s_top);

    C2D_DrawRectangle(0, 0, 0, TOP_WIDTH, 46, C2D_Color32(30, 34, 44, 255));

    if (indigo_ui_text(&title, "Indigo")) {
        C2D_DrawText(&title, C2D_WithColor, 18, 12, 0.0f, 1.0f, 1.0f,
                     C2D_Color32(255, 255, 255, 255));
    }

    if (indigo_ui_text(&subtitle, "Native AT Protocol / Bluesky client")) {
        C2D_DrawText(&subtitle, C2D_WithColor, 18, 62, 0.0f, 0.7f, 0.7f,
                     C2D_Color32(220, 224, 232, 255));
    }

    if (indigo_ui_text(&controls,
                       app->screen == INDIGO_SCREEN_HOME
                           ? "A  Open profile\nB  Return home\nSTART  Exit"
                           : "B  Return home\nSTART  Exit")) {
        C2D_DrawText(&controls, C2D_WithColor, 18, 104, 0.0f, 0.8f, 0.8f,
                     C2D_Color32(255, 255, 255, 255));
    }

    C2D_DrawText(&controls, C2D_WithColor, 18, 190, 0.0f, 0.65f, 0.65f,
                 C2D_Color32(160, 168, 184, 255));

    C2D_TargetClear(s_bottom, C2D_Color32(12, 14, 18, 255));
    C2D_SceneBegin(s_bottom);

    C2D_DrawRectangle(0, 0, 0, BOTTOM_WIDTH, 42, C2D_Color32(30, 34, 44, 255));

    if (indigo_ui_text(&touch, "Touch input")) {
        C2D_DrawText(&touch, C2D_WithColor, 14, 10, 0.0f, 0.75f, 0.75f,
                     C2D_Color32(255, 255, 255, 255));
    }

    char status[96];
    snprintf(status, sizeof(status), "x: %d  y: %d  %s",
             input->touch_x, input->touch_y,
             input->touch_down ? "touching" : "not touching");

    if (indigo_ui_text(&touch, status)) {
        C2D_DrawText(&touch, C2D_WithColor, 14, 58, 0.0f, 0.7f, 0.7f,
                     C2D_Color32(220, 224, 232, 255));
    }

    char sticks[96];
    snprintf(sticks, sizeof(sticks), "Circle: %d, %d\nC-Stick: %d, %d",
             input->circle_x, input->circle_y,
             input->cstick_x, input->cstick_y);

    if (indigo_ui_text(&touch, sticks)) {
        C2D_DrawText(&touch, C2D_WithColor, 14, 104, 0.0f, 0.65f, 0.65f,
                     C2D_Color32(180, 188, 204, 255));
    }

    C3D_FrameEnd(0);
}

void
indigo_ui_shutdown(void)
{
    if (s_text_buf) {
        C2D_TextBufDelete(s_text_buf);
        s_text_buf = NULL;
    }

    s_top = NULL;
    s_bottom = NULL;

    C2D_Fini();
    C3D_Fini();
}
