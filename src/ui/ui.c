#include "ui/ui.h"
#include "gfx/canvas.h"
#include "ui/layout.h"

#include <citro2d.h>

static C3D_RenderTarget *s_top;
static C3D_RenderTarget *s_bottom;
static C2D_TextBuf s_text_buf;

static indigo_canvas s_top_canvas;
static indigo_canvas s_bottom_canvas;

static u32
to_c2d(uint32_t rgba)
{
    return C2D_Color32((rgba >> 24) & 0xff, (rgba >> 16) & 0xff, (rgba >> 8) & 0xff,
                       rgba & 0xff);
}

static void
replay(const indigo_canvas *canvas)
{
    C2D_TextBufClear(s_text_buf);

    for (unsigned i = 0; i < canvas->count; i++) {
        const indigo_cmd *cmd = &canvas->cmds[i];

        if (cmd->kind == INDIGO_CMD_RECT) {
            C2D_DrawRectSolid(cmd->x, cmd->y, 0.0f, cmd->w, cmd->h, to_c2d(cmd->color));
            continue;
        }

        C2D_Text text;
        C2D_TextParse(&text, s_text_buf, indigo_canvas_cmd_text(canvas, cmd));
        C2D_TextOptimize(&text);
        C2D_DrawText(&text, C2D_WithColor, cmd->x, cmd->y, 0.0f, cmd->scale, cmd->scale,
                     to_c2d(cmd->color));
    }
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
    indigo_layout_build(app, input, &s_top_canvas, &s_bottom_canvas);

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    C2D_SceneBegin(s_top);
    replay(&s_top_canvas);

    C2D_SceneBegin(s_bottom);
    replay(&s_bottom_canvas);

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
