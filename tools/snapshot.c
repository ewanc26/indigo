/*
 * Host snapshot renderer. Replays Indigo's display lists into PNGs without a
 * GPU or emulator. Glyphs are baked from a stand-in font, so text shapes and
 * widths approximate the 3DS system font; use the emulator for pixel truth.
 *
 *   snapshot <output-dir>
 */
#include "app/app.h"
#include "gfx/canvas.h"
#include "input/input.h"
#include "ui/layout.h"

#include "snapshot_font.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEXT_SCALE_BASE 1.25f
#define COMPOSITE_GAP 8

typedef struct {
    int w;
    int h;
    uint8_t *px; /* RGBA */
} image;

static image
image_new(int w, int h)
{
    image img = {w, h, calloc((size_t) w * (size_t) h, 4)};

    if (!img.px) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }

    for (int i = 0; i < w * h; i++) {
        img.px[i * 4 + 3] = 255;
    }

    return img;
}

static void
blend(image *img, int x, int y, uint32_t rgba, float coverage)
{
    if (x < 0 || y < 0 || x >= img->w || y >= img->h) {
        return;
    }

    float a = coverage * (float) (rgba & 0xff) / 255.0f;
    uint8_t *p = &img->px[((size_t) y * (size_t) img->w + (size_t) x) * 4];

    for (int c = 0; c < 3; c++) {
        float src = (float) ((rgba >> (24 - 8 * c)) & 0xff);
        p[c] = (uint8_t) lroundf(src * a + (float) p[c] * (1.0f - a));
    }
}

static void
draw_rect(image *img, const indigo_cmd *cmd)
{
    int x0 = (int) lroundf(cmd->x);
    int y0 = (int) lroundf(cmd->y);
    int x1 = (int) lroundf(cmd->x + cmd->w);
    int y1 = (int) lroundf(cmd->y + cmd->h);

    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            blend(img, x, y, cmd->color, 1.0f);
        }
    }
}

static float
sample(const snapshot_glyph *g, float sx, float sy)
{
    if (sx < 0 || sy < 0 || sx > (float) g->w - 1 || sy > (float) g->h - 1) {
        return 0.0f;
    }

    int x0 = (int) sx;
    int y0 = (int) sy;
    int x1 = x0 + 1 < g->w ? x0 + 1 : x0;
    int y1 = y0 + 1 < g->h ? y0 + 1 : y0;
    float fx = sx - (float) x0;
    float fy = sy - (float) y0;
    const uint8_t *d = &snapshot_glyph_data[g->offset];
    float top = (float) d[y0 * g->w + x0] * (1 - fx) + (float) d[y0 * g->w + x1] * fx;
    float bot = (float) d[y1 * g->w + x0] * (1 - fx) + (float) d[y1 * g->w + x1] * fx;

    return (top * (1 - fy) + bot * fy) / 255.0f;
}

static float
draw_run(image *img, const char *text, unsigned from, unsigned to, float pen,
         const indigo_cmd *cmd, uint32_t color)
{
    float scale = cmd->scale * TEXT_SCALE_BASE;

    for (const char *s = text + from; s < text + to; s++) {
        unsigned char ch = (unsigned char) *s;

        if (ch < 32 || ch > 126) {
            ch = '?';
        }

        const snapshot_glyph *g = &snapshot_glyphs[ch - 32];

        if (g->w) {
            int dx0 = (int) floorf(pen + (float) g->x * scale);
            int dy0 = (int) floorf(cmd->y + (float) g->y * scale);
            int dw = (int) ceilf((float) g->w * scale);
            int dh = (int) ceilf((float) g->h * scale);

            for (int y = 0; y < dh; y++) {
                for (int x = 0; x < dw; x++) {
                    float cov = sample(g, ((float) x + 0.5f) / scale - 0.5f,
                                       ((float) y + 0.5f) / scale - 0.5f);

                    if (cov > 0.0f) {
                        blend(img, dx0 + x, dy0 + y, color, cov);
                    }
                }
            }
        }

        pen += (float) g->advance * scale;
    }
    return pen;
}

static void
draw_text(image *img, const indigo_canvas *canvas, const indigo_cmd *cmd)
{
    indigo_segment segs[2 * INDIGO_CANVAS_MAX_SPANS + 1];
    unsigned n = indigo_canvas_segments(canvas, cmd, segs);
    const char *text = indigo_canvas_cmd_text(canvas, cmd);
    float pen = cmd->x;

    for (unsigned i = 0; i < n; i++) {
        pen = draw_run(img, text, segs[i].start, segs[i].end, pen, cmd, segs[i].color);
    }
}

static image
render(const indigo_canvas *canvas)
{
    image img = image_new(canvas->width, canvas->height);

    for (unsigned i = 0; i < canvas->count; i++) {
        const indigo_cmd *cmd = &canvas->cmds[i];

        if (cmd->kind == INDIGO_CMD_RECT) {
            draw_rect(&img, cmd);
        } else {
            draw_text(&img, canvas, cmd);
        }
    }

    return img;
}

static uint32_t
crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    crc = ~crc;

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];

        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }

    return ~crc;
}

static void
put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

static void
write_chunk(FILE *f, const char *type, const uint8_t *data, size_t len)
{
    uint8_t head[8];
    uint8_t tail[4];
    uint32_t crc;

    put32(head, (uint32_t) len);
    memcpy(head + 4, type, 4);
    crc = crc32_update(0, head + 4, 4);
    crc = crc32_update(crc, data, len);
    put32(tail, crc);
    fwrite(head, 1, 8, f);
    fwrite(data, 1, len, f);
    fwrite(tail, 1, 4, f);
}

/* Uncompressed (stored) deflate keeps the writer dependency-free. */
static int
write_png(const char *path, const image *img)
{
    FILE *f = fopen(path, "wb");

    if (!f) {
        return -1;
    }

    size_t row = 1 + (size_t) img->w * 3;
    size_t raw_len = row * (size_t) img->h;
    uint8_t *raw = malloc(raw_len);
    size_t blocks = (raw_len + 65534) / 65535;
    size_t z_len = 2 + raw_len + blocks * 5 + 4;
    uint8_t *z = malloc(z_len);

    if (!raw || !z) {
        fclose(f);
        free(raw);
        free(z);
        return -1;
    }

    for (int y = 0; y < img->h; y++) {
        raw[(size_t) y * row] = 0;

        for (int x = 0; x < img->w; x++) {
            memcpy(&raw[(size_t) y * row + 1 + (size_t) x * 3],
                   &img->px[((size_t) y * (size_t) img->w + (size_t) x) * 4], 3);
        }
    }

    size_t zp = 0;
    uint32_t a = 1;
    uint32_t b = 0;

    z[zp++] = 0x78;
    z[zp++] = 0x01;

    for (size_t off = 0; off < raw_len; off += 65535) {
        size_t n = raw_len - off < 65535 ? raw_len - off : 65535;

        z[zp++] = (off + n >= raw_len) ? 1 : 0;
        z[zp++] = (uint8_t) (n & 0xff);
        z[zp++] = (uint8_t) (n >> 8);
        z[zp++] = (uint8_t) (~n & 0xff);
        z[zp++] = (uint8_t) ((~n >> 8) & 0xff);
        memcpy(z + zp, raw + off, n);
        zp += n;
    }

    for (size_t i = 0; i < raw_len; i++) {
        a = (a + raw[i]) % 65521u;
        b = (b + a) % 65521u;
    }

    put32(z + zp, (b << 16) | a);
    zp += 4;

    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    uint8_t ihdr[13];

    put32(ihdr, (uint32_t) img->w);
    put32(ihdr + 4, (uint32_t) img->h);
    ihdr[8] = 8;
    ihdr[9] = 2;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;

    fwrite(sig, 1, 8, f);
    write_chunk(f, "IHDR", ihdr, sizeof(ihdr));
    write_chunk(f, "IDAT", z, zp);
    write_chunk(f, "IEND", NULL, 0);

    free(raw);
    free(z);
    return fclose(f);
}

/* Both screens as stacked on the console: bottom centred under the top. */
static image
composite(const image *top, const image *bottom)
{
    image out = image_new(top->w, top->h + COMPOSITE_GAP + bottom->h);
    int bx = (top->w - bottom->w) / 2;

    for (int y = 0; y < top->h; y++) {
        memcpy(&out.px[(size_t) y * (size_t) out.w * 4], &top->px[(size_t) y * (size_t) top->w * 4],
               (size_t) top->w * 4);
    }

    for (int y = 0; y < bottom->h; y++) {
        memcpy(&out.px[((size_t) (top->h + COMPOSITE_GAP + y) * (size_t) out.w + (size_t) bx) * 4],
               &bottom->px[(size_t) y * (size_t) bottom->w * 4], (size_t) bottom->w * 4);
    }

    return out;
}

typedef struct {
    const char *name;
    indigo_screen screen;
    bool touching;
    int touch_x;
    int touch_y;
    int timeline; /* 0 none, 1 populated, 2 loading, 3 error */
    unsigned select;
} scenario;

/* Facet byte ranges follow the text, the way Wolfram reports them. */
static void
add_facet(indigo_post *p, indigo_facet_kind kind, const char *needle, const char *target)
{
    const char *at = strstr(p->text, needle);

    if (!at || p->facet_count >= INDIGO_POST_FACETS_MAX) {
        return;
    }
    {
        indigo_post_facet *f = &p->facets[p->facet_count];

        f->kind = kind;
        f->start = (unsigned) (at - p->text);
        f->end = f->start + (unsigned) strlen(needle);
        indigo_copy_utf8(f->target, sizeof f->target, target);
        p->facet_count++;
    }
}

static void
add_post(indigo_timeline *t, const char *name, const char *handle, const char *text,
         const char *reposter, unsigned likes, bool liked, bool link)
{
    indigo_post p;

    memset(&p, 0, sizeof p);
    snprintf(p.uri, sizeof p.uri, "at://did:plc:fake/app.bsky.feed.post/%u", t->count);
    snprintf(p.cid, sizeof p.cid, "bafy%u", t->count);
    indigo_copy_utf8(p.display_name, sizeof p.display_name, name);
    indigo_copy_utf8(p.handle, sizeof p.handle, handle);
    indigo_copy_utf8(p.text, sizeof p.text, text);
    indigo_copy_utf8(p.reposted_by, sizeof p.reposted_by, reposter);
    p.like_count = likes;
    p.repost_count = likes / 3;
    p.reply_count = likes / 5;
    if (liked) {
        indigo_copy_utf8(p.like_uri, sizeof p.like_uri, "at://did:plc:fake/app.bsky.feed.like/1");
    }
    if (link) {
        add_facet(&p, INDIGO_FACET_LINK, "https://github.com/ewanc26/indigo",
                  "https://github.com/ewanc26/indigo");
        indigo_copy_utf8(p.embed_note, sizeof p.embed_note, "Link card: Wolfram on GitHub");
    }
    indigo_timeline_append(t, &p);
}

static void
fill_timeline(indigo_timeline *t, int kind, unsigned select)
{
    indigo_timeline_init(t);
    if (kind == 0) {
        return; /* no posts: the menu then offers only its app actions */
    }
    if (kind == 2) {
        indigo_timeline_begin_fetch(t, true);
        return;
    }
    if (kind == 3) {
        indigo_timeline_fail_fetch(t, "Could not reach the network.");
        return;
    }
    add_post(t, "Ewan Croft", "ewancroft.uk",
             "@rhi.example.social thanks for the bug reports. Read more at "
             "https://github.com/ewanc26/indigo and tag it #indigo3ds. The 3DS is "
             "surprisingly pleasant to write C for.",
             "", 42, false, true);
    {
        /* Mentions and tags only exist in the sample post, to exercise the
         * More menu's facet targets. */
        indigo_post *p = &t->posts[0];

        add_facet(p, INDIGO_FACET_MENTION, "@rhi.example.social",
                  "did:plc:rhi0000000000000000000000000");
        add_facet(p, INDIGO_FACET_TAG, "#indigo3ds", "indigo3ds");
    }
    add_post(t, "Rhiannon", "rhi.example.social", "Morning walk by the river, very cold and very clear.",
             "Ewan Croft", 7, true, false);
    add_post(t, "Cobalt", "cobalt.example", "Wii U client update: the feed now parses through Wolfram.",
             "", 18, false, false);
    add_post(t, "A very long display name that keeps going", "long.handle.example.com",
             "Short one.", "", 3, false, false);
    add_post(t, "Dev Log", "devlog.example", "Fifth post, to prove scrolling keeps the selection visible.",
             "", 0, false, false);
    indigo_timeline_finish_fetch(t, "cursor");
    indigo_timeline_select(t, select, INDIGO_TIMELINE_ROWS);
}

static void
fill_social(indigo_app *app, const scenario *s)
{
    if (s->screen == INDIGO_SCREEN_THREAD) {
        fill_timeline(&app->thread, 1, s->select);
        app->thread_focus = 1;
    } else if (s->screen == INDIGO_SCREEN_PROFILE) {
        indigo_profile *p = &app->profile;

        indigo_copy_utf8(p->handle, sizeof p->handle, "rhi.example.social");
        indigo_copy_utf8(p->display_name, sizeof p->display_name, "Rhiannon");
        indigo_copy_utf8(p->bio, sizeof p->bio,
                         "Walker, reader, occasional poet. Rivers before roads, always. "
                         "Writing about the Welsh borders and old stones.");
        indigo_copy_utf8(p->did, sizeof p->did, "did:plc:rhiannon7wvx2m4qz6kbyt");
        if (s->timeline == 1) {
            /* Not following: the button offers the action. */
            p->following = false;
            p->follow_uri[0] = '\0';
        } else if (s->timeline == 2) {
            /* Mid-flight after pressing Follow: the label shows progress so
             * a second press is visibly ignored rather than looking dead. */
            p->following = false;
            p->follow_busy = true;
        } else if (s->timeline == 3) {
            /* Muted: both toggles name the action they will perform. */
            p->muted = true;
            p->following = true;
            indigo_copy_utf8(p->follow_uri, sizeof p->follow_uri,
                             "at://did:plc:rhiannon7wvx2m4qz6kbyt/app.bsky.graph.follow/"
                             "self/3kqz9d2f7xw4");
        }
        p->followers = 1204;
        p->follows = 310;
        p->posts = 5821;
        p->loaded = true;
        if (s->timeline != 1 && s->timeline != 2 && s->timeline != 3) {
            p->following = true;
            indigo_copy_utf8(p->follow_uri, sizeof p->follow_uri,
                             "at://did:plc:rhiannon7wvx2m4qz6kbyt/app.bsky.graph.follow/"
                             "self/3kqz9d2f7xw4");
        }
    } else if (s->screen == INDIGO_SCREEN_NOTIFICATIONS) {
        indigo_notification n[3];

        memset(n, 0, sizeof n);
        n[0].kind = INDIGO_NOTE_REPLY;
        n[0].unread = true;
        indigo_copy_utf8(n[0].name, sizeof n[0].name, "Rhiannon");
        indigo_copy_utf8(n[0].handle, sizeof n[0].handle, "rhi.example.social");
        indigo_copy_utf8(n[0].text, sizeof n[0].text, "Congratulations, this looks great on the 3DS!");
        n[1].kind = INDIGO_NOTE_LIKE;
        indigo_copy_utf8(n[1].name, sizeof n[1].name, "Cobalt");
        indigo_copy_utf8(n[1].handle, sizeof n[1].handle, "cobalt.example");
        n[2].kind = INDIGO_NOTE_FOLLOW;
        indigo_copy_utf8(n[2].handle, sizeof n[2].handle, "devlog.example");
        indigo_app_notifications_loaded(app, n, 3);
    } else if (s->screen == INDIGO_SCREEN_SEARCH) {
        indigo_search *q = &app->search;
        indigo_actor a[3];

        memset(a, 0, sizeof a);
        if (s->timeline == 8) {
            /* A person's own posts: same rows as a post search, but the header
             * names them and there is no query to type. */
            indigo_post ps[2];

            q->kind = INDIGO_SEARCH_AUTHOR;
            indigo_copy_utf8(q->subject, sizeof q->subject, "rhi.example.social");
            memset(ps, 0, sizeof ps);
            snprintf(ps[0].uri, sizeof ps[0].uri, "at://did:plc:rhiannon7wvx2m4qz6kbyt/app.bsky.feed.post/3kqz");
            snprintf(ps[0].handle, sizeof ps[0].handle, "rhi.example.social");
            snprintf(ps[0].display_name, sizeof ps[0].display_name, "Rhiannon");
            snprintf(ps[0].text, sizeof ps[0].text,
                     "Rivers before roads, always. A walk along the Wye at first light.");
            snprintf(ps[0].reposted_by, sizeof ps[0].reposted_by, "Rhiannon Bear");
            ps[0].reply_count = 12;
            ps[0].repost_count = 34;
            ps[0].like_count = 210;
            snprintf(ps[1].uri, sizeof ps[1].uri, "at://did:plc:rhiannon7wvx2m4qz6kbyt/app.bsky.feed.post/9d2f");
            snprintf(ps[1].handle, sizeof ps[1].handle, "rhi.example.social");
            snprintf(ps[1].display_name, sizeof ps[1].display_name, "Rhiannon");
            snprintf(ps[1].text, sizeof ps[1].text,
                     "Old stones and newer roads, and the argument about which came first.");
            ps[1].is_reply = true;
            ps[1].reply_count = 4;
            ps[1].like_count = 61;
            indigo_app_post_search_loaded(app, ps, 2);
            return;
        }
        if (s->timeline == 6 || s->timeline == 7) {
            /* Post search: same screen, but rows lead with the author and
             * the top screen shows the post rather than a profile. */
            indigo_post ps[2];

            q->kind = INDIGO_SEARCH_POSTS;
            indigo_copy_utf8(q->query, sizeof q->query, s->timeline == 6 ? "welsh borders" : "");
            memset(ps, 0, sizeof ps);
            if (s->timeline == 6) {
                snprintf(ps[0].uri, sizeof ps[0].uri, "at://did:plc:rhiannon7wvx2m4qz6kbyt/app.bsky.feed.post/3kqz");
                snprintf(ps[0].handle, sizeof ps[0].handle, "rhi.example.social");
                snprintf(ps[0].display_name, sizeof ps[0].display_name, "Rhiannon");
                snprintf(ps[0].text, sizeof ps[0].text,
                         "Rivers before roads, always. A walk along the Wye at first "
                         "light, and the old stones doing what they have always done, "
                         "which is nothing at all, patiently.");
                ps[0].reply_count = 12;
                ps[0].repost_count = 34;
                ps[0].like_count = 210;
                snprintf(ps[1].uri, sizeof ps[1].uri, "at://did:plc:bear4kq8vz2n7xwm3/app.bsky.feed.post/9d2f");
                snprintf(ps[1].handle, sizeof ps[1].handle, "rhibear.example.social");
                snprintf(ps[1].display_name, sizeof ps[1].display_name, "Rhiannon Bear");
                snprintf(ps[1].text, sizeof ps[1].text,
                         "Drawn from that same walk. Ink on board, as usual, and not "
                         "one straight line in the whole thing.");
                ps[1].reply_count = 3;
                ps[1].repost_count = 7;
                ps[1].like_count = 88;
            }
            indigo_app_post_search_loaded(app, ps, s->timeline == 6 ? 2u : 0u);
            return;
        }
        if (s->timeline == 4 || s->timeline == 5) {
            /* The followers and following lists: same screen and rows as a
             * person search, with the subject named in the header instead of
             * a query box. */
            q->kind = s->timeline == 4 ? INDIGO_SEARCH_FOLLOWERS : INDIGO_SEARCH_FOLLOWING;
            indigo_copy_utf8(q->subject, sizeof q->subject, "rhi.example.social");
            indigo_copy_utf8(a[0].handle, sizeof a[0].handle, "rhibear.example.social");
            indigo_copy_utf8(a[0].display_name, sizeof a[0].display_name, "Rhiannon Bear");
            indigo_copy_utf8(a[0].did, sizeof a[0].did, "did:plc:bear4kq8vz2n7xwm3");
            indigo_copy_utf8(a[1].handle, sizeof a[1].handle, "rhidraws.example");
            indigo_copy_utf8(a[1].display_name, sizeof a[1].display_name, "Rhi Draws");
            indigo_copy_utf8(a[1].did, sizeof a[1].did, "did:plc:draws9mqx4v2k7b");
            indigo_app_search_loaded(app, a, 2);
            return;
        }
        indigo_copy_utf8(q->query, sizeof q->query, "rhi");
        if (s->timeline == 1) {
            indigo_copy_utf8(a[0].handle, sizeof a[0].handle, "rhi.example.social");
            indigo_copy_utf8(a[0].display_name, sizeof a[0].display_name, "Rhiannon");
            indigo_copy_utf8(a[0].did, sizeof a[0].did,
                             "did:plc:rhiannon7wvx2m4qz6kbyt");
            indigo_copy_utf8(a[1].handle, sizeof a[1].handle, "rhibear.example.social");
            indigo_copy_utf8(a[1].display_name, sizeof a[1].display_name, "Rhiannon Bear");
            indigo_copy_utf8(a[1].did, sizeof a[1].did, "did:plc:bear4kq8vz2n7xwm3");
            indigo_copy_utf8(a[2].handle, sizeof a[2].handle, "rhidraws.example");
            indigo_copy_utf8(a[2].display_name, sizeof a[2].display_name, "Rhi Draws");
            indigo_copy_utf8(a[2].did, sizeof a[2].did, "did:plc:draws9mqx4v2k7b");
            indigo_app_search_loaded(app, a, 3);
            if (s->select < 3) {
                q->selected = s->select;
            }
        } else if (s->timeline == 2) {
            /* Searched, and nobody matched: not the same screen as untried. */
            indigo_app_search_loaded(app, NULL, 0);
        } else if (s->timeline == 3) {
            indigo_app_search_failed(app, "Could not reach the server.");
        }
    } else if (s->screen == INDIGO_SCREEN_COMPOSE) {
        indigo_compose *c = &app->compose;

        if (s->timeline == 1) {
            c->mode = INDIGO_COMPOSE_REPLY;
            c->has_target = true;
            c->target = app->timeline.posts[0];
            indigo_copy_utf8(c->text, sizeof c->text,
                             "Thanks! More screens are coming: threads, profiles and notifications.");
        }
    } else if (s->screen == INDIGO_SCREEN_MENU) {
        /* The menu is built from the post being read; `select` walks down it. */
        indigo_menu_build(&app->menu, indigo_timeline_selected(&app->timeline),
                          app->signin.account);
        for (unsigned i = 0; i < s->select; i++) {
            indigo_menu_move(&app->menu, 1, INDIGO_MENU_ROWS);
        }
    }
}

int
main(int argc, char **argv)
{
    static const scenario scenarios[] = {
        {"signin", INDIGO_SCREEN_SIGNIN, false, 0, 0, 0, 0},
        {"timeline", INDIGO_SCREEN_HOME, false, 0, 0, 1, 0},
        {"timeline-scrolled", INDIGO_SCREEN_HOME, false, 0, 0, 1, 4},
        {"timeline-loading", INDIGO_SCREEN_HOME, false, 0, 0, 2, 0},
        {"timeline-error", INDIGO_SCREEN_HOME, false, 0, 0, 3, 0},
        {"thread", INDIGO_SCREEN_THREAD, false, 0, 0, 1, 1},
        {"profile", INDIGO_SCREEN_PROFILE, false, 0, 0, 0, 0},
        {"profile-follow", INDIGO_SCREEN_PROFILE, false, 0, 0, 1, 0},
        {"profile-following", INDIGO_SCREEN_PROFILE, false, 0, 0, 2, 0},
        {"profile-muted", INDIGO_SCREEN_PROFILE, false, 0, 0, 3, 0},
        {"notifications", INDIGO_SCREEN_NOTIFICATIONS, false, 0, 0, 0, 0},
        {"menu", INDIGO_SCREEN_MENU, false, 0, 0, 0, 0},
        {"menu-facets", INDIGO_SCREEN_MENU, false, 0, 0, 1, 0},
        {"menu-facets-scrolled", INDIGO_SCREEN_MENU, false, 0, 0, 1, 5},
        {"compose-reply", INDIGO_SCREEN_COMPOSE, false, 0, 0, 1, 0},
        {"compose-empty", INDIGO_SCREEN_COMPOSE, false, 0, 0, 0, 0},
        {"search", INDIGO_SCREEN_SEARCH, false, 0, 0, 0, 0},
        {"search-results", INDIGO_SCREEN_SEARCH, false, 0, 0, 1, 1},
        {"search-none", INDIGO_SCREEN_SEARCH, false, 0, 0, 2, 0},
        {"search-error", INDIGO_SCREEN_SEARCH, false, 0, 0, 3, 0},
        {"search-followers", INDIGO_SCREEN_SEARCH, false, 0, 0, 4, 0},
        {"search-following", INDIGO_SCREEN_SEARCH, false, 0, 0, 5, 0},
        {"post-search", INDIGO_SCREEN_SEARCH, false, 0, 0, 6, 0},
        {"post-search-empty", INDIGO_SCREEN_SEARCH, false, 0, 0, 7, 0},
        {"author-posts", INDIGO_SCREEN_SEARCH, false, 0, 0, 8, 0},
    };

    if (argc != 2) {
        fprintf(stderr, "usage: %s <output-dir>\n", argv[0]);
        return 2;
    }

    for (size_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); i++) {
        const scenario *s = &scenarios[i];
        static indigo_app app;
        indigo_input input = {0};
        static indigo_canvas top_canvas;
        static indigo_canvas bottom_canvas;

        indigo_app_init(&app);
        app.screen = s->screen;
        app.wolfram_linked = true;
        if (s->screen != INDIGO_SCREEN_SIGNIN) {
            strcpy(app.signin.account, "ewancroft.uk");
        }
        fill_timeline(&app.timeline, s->timeline, s->select);
        fill_social(&app, s);
        input.touch_down = s->touching;
        input.touch_x = s->touch_x;
        input.touch_y = s->touch_y;

        indigo_layout_build(&app, &input, &top_canvas, &bottom_canvas);

        image top = render(&top_canvas);
        image bottom = render(&bottom_canvas);
        image both = composite(&top, &bottom);
        const image *outputs[] = {&top, &bottom, &both};
        const char *suffix[] = {"top", "bottom", "both"};

        for (int k = 0; k < 3; k++) {
            char path[1024];

            snprintf(path, sizeof(path), "%s/%s-%s.png", argv[1], s->name, suffix[k]);

            if (write_png(path, outputs[k]) != 0) {
                fprintf(stderr, "could not write %s\n", path);
                return 1;
            }

            printf("%s\n", path);
        }

        free(top.px);
        free(bottom.px);
        free(both.px);
    }

    return 0;
}
