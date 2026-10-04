#include "ui/ui.h"
#include "gfx/canvas.h"
#include "media/media.h"
#include "media/media_loader.h"
#include "ui/layout.h"
#include "util/log.h"

#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static C3D_RenderTarget *s_top;
static C3D_RenderTarget *s_bottom;
static C2D_TextBuf s_text_buf;

static indigo_canvas s_top_canvas;
static indigo_canvas s_bottom_canvas;

/* The decoded-image cache and the GPU textures over it.
 *
 * A texture is never created off the frame loop and never destroyed anywhere
 * else: uploads happen here, on the main thread, inside C3D_FrameBegin. The
 * cache owns the pixels, this owns the GPU copy, and the two are tied together
 * by the pixel pointer -- when a slot is evicted and reloaded the pointer
 * changes, which is what tells a texture it is showing stale content.
 *
 * Only images on screen are ever uploaded, and the widest screen shows six (a
 * post's author and picture, three rows, and a profile). The viewer needs one
 * more on top of the screen it covers, because it does not replace it -- the
 * screen behind it stays in the cache and its textures stay uploaded until
 * something evicts them -- so the count has to leave room for one large
 * texture beside a screen's worth of small ones.
 *
 * The byte bound is the one that matters. A texture is power-of-two padded, so
 * an avatar costs a 128x128 and a full-size photograph a 512x512: a pool of
 * eight full-size textures would be 32MB of VRAM, which the console does not
 * have, while the same pool of eight avatars is half a megabyte. Both bounds
 * are sized so the viewer always fits beside the screen it covers -- four
 * avatars and a thumbnail are about 400KB, and the viewer is at most 1MB -- and
 * so that a screen which somehow asked for more than that draws placeholders
 * instead of failing to allocate. */
#define UI_TEX_MAX 12
#define UI_TEX_BYTES_MAX (2u * 1024u * 1024u)

static indigo_media_cache s_media;

typedef struct {
    char url[INDIGO_CANVAS_IMAGE_URL_MAX];
    const uint8_t *pixels;
    unsigned width;
    unsigned height;
    /* The padded size, which is what the texture costs; the pixel width and
     * height are the sub-texture inside it. */
    unsigned tex_w;
    unsigned tex_h;
    C3D_Tex tex;
    Tex3DS_SubTexture sub;
} ui_tex;

static ui_tex s_texs[UI_TEX_MAX];
static unsigned s_tex_bytes;

/* The URL the viewer last asked the cache to re-decode, so a picture is only
 * dropped once per visit rather than every frame. Held at the width a post
 * stores its embed URL rather than the width the canvas draws it, so the
 * comparison cannot truncate a long CDN path into looking like another one. */
static char s_viewer_url[INDIGO_EMBED_URL_MAX];

/* The viewer draws the picture at the top screen's width, where the detail band
 * drew it inside 364x60. The cache decodes a URL once, at the first size asked
 * for, so a portrait photograph the band showed 60px tall would still be 60px
 * here -- four times too small for the screen, and a blur is not a picture.
 * Dropping the slot makes the next claim decode it at the size this screen
 * draws, and the screen that wanted the smaller copy is not on screen while the
 * viewer is. */
static void
viewer_prepare(const indigo_app *app)
{
    unsigned want = INDIGO_TOP_WIDTH;
    int slot;

    if (app->screen != INDIGO_SCREEN_IMAGE || !app->image.url[0]) {
        return;
    }
    if (strcmp(s_viewer_url, app->image.url) == 0) {
        return;
    }
    snprintf(s_viewer_url, sizeof s_viewer_url, "%s", app->image.url);
    slot = indigo_media_slot_of(&s_media, app->image.url);
    if (slot >= 0 && s_media.slots[slot].max_dim >= want) {
        return;
    }
    indigo_media_forget(&s_media, app->image.url);
}

static u32
to_c2d(uint32_t rgba)
{
    return C2D_Color32((rgba >> 24) & 0xff, (rgba >> 16) & 0xff, (rgba >> 8) & 0xff,
                       rgba & 0xff);
}

static void
tex_release(ui_tex *t)
{
    if (t->pixels) {
        s_tex_bytes -= t->tex_w * t->tex_h * 4u;
        C3D_TexDelete(&t->tex);
    }
    memset(t, 0, sizeof *t);
}

/* Drops every texture whose cache slot is gone or has been replaced. Called
 * once per frame, before drawing: a texture outliving its pixels would draw
 * freed memory, and holding one for an image no longer on screen is exactly
 * the sort of unbounded growth AGENTS.md §17 warns about. */
static void
tex_reconcile(void)
{
    for (unsigned i = 0; i < UI_TEX_MAX; i++) {
        ui_tex *t = &s_texs[i];
        int slot;

        if (!t->pixels) {
            continue;
        }
        slot = indigo_media_ready(&s_media, t->url);
        if (slot < 0 || s_media.slots[slot].pixels != t->pixels) {
            tex_release(t);
        }
    }
}

/* citro3d only allocates power-of-two textures between 8 and 1024, so a
 * decoded image is copied into the top-left corner of one. Power-of-two also
 * costs nothing here: the decoded cap is a power of two, and a non-square
 * avatar or a letterboxed thumbnail is padded rather than stretched. */
static unsigned
pot_up(unsigned v)
{
    unsigned p = 8;

    while (p < v && p < 1024u) {
        p <<= 1;
    }
    return p;
}

static ui_tex *
tex_for(unsigned slot)
{
    const indigo_media_slot *s = &s_media.slots[slot];
    unsigned tex_w, tex_h;

    for (unsigned i = 0; i < UI_TEX_MAX; i++) {
        if (s_texs[i].pixels == s->pixels && s_texs[i].pixels) {
            return &s_texs[i];
        }
    }
    for (unsigned i = 0; i < UI_TEX_MAX; i++) {
        if (s_texs[i].pixels) {
            continue;
        }
        tex_w = pot_up(s->width);
        tex_h = pot_up(s->height);
        /* The byte bound, refused before the allocation rather than after it:
         * a pool that grew until citro3d said no would leave the textures it
         * could not replace deleted out from under the frame. */
        if (s_tex_bytes + tex_w * tex_h * 4u > UI_TEX_BYTES_MAX) {
            return NULL;
        }
        if (!C3D_TexInit(&s_texs[i].tex, (u16) tex_w, (u16) tex_h, GPU_RGBA8)) {
            return NULL;
        }
        {
            unsigned char *padded = calloc(tex_w * tex_h, 4);

            if (!padded) {
                C3D_TexDelete(&s_texs[i].tex);
                return NULL;
            }
            for (unsigned y = 0; y < s->height; y++) {
                memcpy(padded + (size_t) y * tex_w * 4u, s->pixels + (size_t) y * s->width * 4u,
                       (size_t) s->width * 4u);
            }
            C3D_TexLoadImage(&s_texs[i].tex, padded, GPU_TEXFACE_2D, 0);
            C3D_TexFlush(&s_texs[i].tex);
            free(padded);
        }
        /* Bilinear: avatars are drawn well below their decoded size, and
         * nearest-neighbour at that ratio is a shimmering mess. */
        C3D_TexSetFilter(&s_texs[i].tex, GPU_LINEAR, GPU_LINEAR);
        C3D_TexSetWrap(&s_texs[i].tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        memcpy(s_texs[i].url, s->url, sizeof s_texs[i].url);
        s_texs[i].pixels = s->pixels;
        s_texs[i].width = s->width;
        s_texs[i].height = s->height;
        s_texs[i].tex_w = tex_w;
        s_texs[i].tex_h = tex_h;
        s_tex_bytes += tex_w * tex_h * 4u;
        s_texs[i].sub = (Tex3DS_SubTexture) {
            .width = (u16) s->width,
            .height = (u16) s->height,
            .left = 0.0f,
            .top = 0.0f,
            .right = (float) s->width / (float) tex_w,
            .bottom = (float) s->height / (float) tex_h,
        };
        return &s_texs[i];
    }
    return NULL;
}

static void
draw_image(const indigo_cmd *cmd, const indigo_canvas_image_ref *img)
{
    int slot = indigo_media_ready(&s_media, img->url);
    ui_tex *t;
    C2D_Image image;

    if (slot < 0) {
        /* Ask for it whether it is missing or in flight: the cache drops the
         * request when it already knows the URL, so this is one lookup rather
         * than a fetch storm. */
        indigo_media_loader_request(&s_media, img->url,
                                    (unsigned) (cmd->w > cmd->h ? cmd->w : cmd->h));
        C2D_DrawRectSolid(cmd->x, cmd->y, 0.0f, cmd->w, cmd->h, to_c2d(cmd->color));
        return;
    }
    t = tex_for((unsigned) slot);
    if (!t) {
        C2D_DrawRectSolid(cmd->x, cmd->y, 0.0f, cmd->w, cmd->h, to_c2d(cmd->color));
        return;
    }
    image = (C2D_Image) { .tex = &t->tex, .subtex = &t->sub };
    C2D_DrawImageAt(image, cmd->x, cmd->y, 0.0f, NULL, cmd->w / (float) t->width,
                    cmd->h / (float) t->height);
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
        if (cmd->kind == INDIGO_CMD_IMAGE) {
            draw_image(cmd, &canvas->images[cmd->image_index]);
            continue;
        }

        indigo_segment segs[2 * INDIGO_CANVAS_MAX_SPANS + 1];
        unsigned n = indigo_canvas_segments(canvas, cmd, segs);
        const char *full = indigo_canvas_cmd_text(canvas, cmd);
        float pen = cmd->x;

        for (unsigned k = 0; k < n; k++) {
            char piece[INDIGO_CANVAS_SEGMENT_MAX];
            unsigned len = segs[k].end - segs[k].start;
            C2D_Text text;
            float w;

            if (len >= sizeof piece) {
                len = sizeof piece - 1;
            }
            memcpy(piece, full + segs[k].start, len);
            piece[len] = '\0';

            C2D_TextParse(&text, s_text_buf, piece);
            C2D_TextOptimize(&text);
            C2D_DrawText(&text, C2D_WithColor, pen, cmd->y, 0.0f, cmd->scale, cmd->scale,
                         to_c2d(segs[k].color));
            C2D_TextGetDimensions(&text, cmd->scale, cmd->scale, &w, NULL);
            pen += w;
        }
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
    s_text_buf = C2D_TextBufNew(4096);

    if (!s_top || !s_bottom || !s_text_buf) {
        indigo_ui_shutdown();
        return false;
    }

    indigo_media_init(&s_media);
    /* Not fatal: without it every avatar stays a placeholder forever, which is
     * ugly but not wrong, so the failure is reported rather than fatal. */
    if (!indigo_media_loader_start()) {
        indigo_log_warn("media loader unavailable; images will not load");
    }

    return true;
}

void
indigo_ui_draw(const indigo_app *app, const indigo_input *input)
{
    /* Before layout: a decode that finished this frame should be drawable
     * this frame rather than one frame later. */
    indigo_media_loader_drain(&s_media);
    viewer_prepare(app);
    indigo_layout_build(app, input, &s_top_canvas, &s_bottom_canvas);
    tex_reconcile();

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
    for (unsigned i = 0; i < UI_TEX_MAX; i++) {
        tex_release(&s_texs[i]);
    }
    indigo_media_loader_stop();
    indigo_media_clear(&s_media);

    if (s_text_buf) {
        C2D_TextBufDelete(s_text_buf);
        s_text_buf = NULL;
    }

    s_top = NULL;
    s_bottom = NULL;

    C2D_Fini();
    C3D_Fini();
}