/* Drawing a post: its text, its picture or link card, its counters. */

#include "ui/layout_internal.h"

const char *
indigo_layout_author_name(const indigo_post *p)
{
    return p->display_name[0] ? p->display_name : p->handle;
}

/* Whether the header bar is showing the viewer button, which is what costs the
 * status text its slot. One predicate for the bottom screen that draws it and
 * the top screen that gives the status up, so the two cannot disagree about
 * which of them is showing what. */
static bool
image_button_shown(const indigo_app *app)
{
    return indigo_post_has_image(indigo_app_image_source(app));
}

/* The button that opens the viewer. Returns whether it was drawn, which is what
 * the top screen asks before it takes the status text over from its own hint
 * line. */
bool
indigo_layout_image_button(indigo_canvas *c, const indigo_app *app)
{
    indigo_rect r;

    if (!image_button_shown(app)) {
        return false;
    }
    r = indigo_layout_button_rect(INDIGO_ACTION_IMAGE);
    indigo_canvas_rect(c, r.x, r.y, r.w, r.h, COL_PILL_ACTIVE);
    /* Named for the action and not the key, like the Profile pill: every one
     * of these controls is on the touchscreen, and ZR is a shortcut only a
     * New 3DS has rather than the way in. */
    indigo_canvas_text(c, r.x + 26, r.y + 7, 0.6f, COL_TEXT, "Image");
    return true;
}

/* Draw `text` as wrapped lines, colouring facet ranges. `max_lines` is the
 * budget the caller has: a post with an embed gives up three of its five. */
static void
draw_post_text(indigo_canvas *c, const indigo_post *p, unsigned max_lines,
               unsigned text_scale)
{
    indigo_line lines[POST_TEXT_LINES];
    int truncated;
    /* The text-size setting scales the post body only: it is the reading
     * surface, and the chrome around it has fixed room. Normal (115) is the
     * layout's own 0.6, so the default is unchanged. Line pitch scales with
     * the glyphs and the block keeps its vertical budget by showing fewer
     * lines when they are taller, so the embed band below never moves. */
    float scale = POST_TEXT_SCALE * (float) text_scale / (float) INDIGO_TEXT_SCALE_NORMAL;
    float pitch = (float) POST_LINE_PITCH * scale / POST_TEXT_SCALE;
    unsigned fit = (unsigned) ((float) (POST_LINE_PITCH * max_lines) / pitch);
    unsigned units = (unsigned) ((INDIGO_TOP_WIDTH - 2 * POST_TEXT_X) /
                                 (INDIGO_CHAR_WIDTH * scale));
    unsigned n;

    if (fit < max_lines) {
        max_lines = fit > 0 ? fit : 1;
    }
    n = indigo_wrap(p->text, units, lines, max_lines, &truncated);

    for (unsigned i = 0; i < n; i++) {
        const char *at = p->text + lines[i].start;
        unsigned len = lines[i].len;
        bool last = truncated && i + 1 == n;

        /* %.*s keeps this to the line; the ellipsis marks cut-off text. */
        indigo_canvas_text(c, POST_TEXT_X, POST_TEXT_Y + pitch * (float) i,
                           scale, COL_TEXT, "%.*s%s", (int) len, at,
                           last ? "..." : "");
        for (unsigned f = 0; f < p->facet_count; f++) {
            unsigned s = p->facets[f].start;
            unsigned e = p->facets[f].end;

            if (e <= lines[i].start || s >= lines[i].start + len) {
                continue;
            }
            s = s < lines[i].start ? lines[i].start : s;
            e = e > lines[i].start + len ? lines[i].start + len : e;
            indigo_canvas_span(c, s - lines[i].start, e - lines[i].start, COL_LINK);
        }
    }
}

/* True when the post has an embed the screen draws as itself rather than as a
 * line of text. A quote and an attachment Indigo cannot draw keep the one-line
 * note instead; a video is drawn as its poster frame. */
static bool
post_draws_embed(const indigo_post *p)
{
    if (p->embed_kind == INDIGO_EMBED_IMAGE || p->embed_kind == INDIGO_EMBED_VIDEO) {
        return p->embed_thumb[0] != '\0';
    }
    if (p->embed_kind == INDIGO_EMBED_LINK) {
        return p->embed_uri[0] != '\0';
    }
    return false;
}

/* A box of the image's own shape inside `band_h` of the band, centred on it.
 * The aspect ratio is the server's; when it declared none, a square is the
 * assumption that distorts least, because the box drawn is stretched to
 * whatever shape it is given. */
static void
draw_post_image(indigo_canvas *c, const indigo_post *p, float band_h)
{
    float aw = p->embed_w > 0 && p->embed_h > 0 ? (float) p->embed_w : 1.0f;
    float ah = p->embed_w > 0 && p->embed_h > 0 ? (float) p->embed_h : 1.0f;
    /* Fit the declared shape inside the band on both axes. Scaling by the
     * smaller of the two ratios is what keeps a 1:3 panorama from being 180px
     * tall: the box is drawn stretched to whatever shape it is given, so a box
     * outside the band is a box off the screen. */
    float k = EMBED_W / aw;
    float by_height = band_h / ah;
    float w;
    float h;

    if (by_height < k) {
        k = by_height;
    }
    w = aw * k;
    h = ah * k;
    indigo_canvas_image(c, EMBED_X + (EMBED_W - w) / 2.0f, EMBED_Y, w, h,
                        p->embed_thumb, indigo_media_placeholder_color(p->embed_thumb));
    /* Bluesky allows four images and Indigo draws one, so a post of several says
     * so rather than quietly showing one of them as if it were all of them. */
    if (p->embed_count > 1) {
        /* On the band's own bottom edge, so it moves up with the picture when
         * alt text has taken lines out of it. */
        indigo_canvas_text(c, 330, EMBED_Y + band_h - 8.0f, ALT_SCALE, COL_TEXT_DIM,
                           "+%u more", (unsigned) p->embed_count - 1u);
    }
}

/* Alt text under a picture, when the setting is on and the author wrote any.
 *
 * It takes what it needs rather than a fixed share: one sentence costs one line,
 * a long description costs the two that fit, and the picture is fitted into
 * whatever is left of the band. A setting that made every photograph a quarter
 * of the screen smaller would not be worth turning on.
 *
 * Alt text is drawn in the dim colour and at the small scale deliberately. It
 * is reference, not caption: the post's own text is the thing being read, and
 * an accessibility option that competes with it is not one. */
static float
draw_post_alt(indigo_canvas *c, const char *alt)
{
    static const float pitch = 13.0f;
    indigo_line lines[2];
    int truncated;
    unsigned units = (unsigned) (EMBED_W / (INDIGO_CHAR_WIDTH * ALT_SCALE));
    unsigned n = indigo_wrap(alt, units, lines, 2, &truncated);

    for (unsigned i = 0; i < n; i++) {
        indigo_canvas_text(c, EMBED_X,
                           EMBED_Y + EMBED_H - (float) (n - i) * pitch, ALT_SCALE,
                           COL_TEXT_DIM, "%.*s%s", (int) lines[i].len,
                           alt + lines[i].start, truncated && i + 1 == n ? "..." : "");
    }
    return (float) n * pitch;
}

/* A link card: the title over the place it points at, with the link's own
 * picture on the right when the card has one. Indigo cannot open a link yet, so
 * this is something to read rather than something to press -- hence a surface
 * colour rather than a control's. */
static void
draw_post_link(indigo_canvas *c, const indigo_post *p)
{
    float text_x = EMBED_X + (float) CARD_INSET;
    float text_w = EMBED_W - 2.0f * (float) CARD_INSET;
    const char *title = p->embed_title[0] ? p->embed_title : p->embed_uri;
    indigo_line lines[2];
    int truncated;
    unsigned units;
    unsigned n;
    float y;

    if (p->embed_thumb[0]) {
        text_w -= (float) (CARD_THUMB + CARD_INSET);
    }
    indigo_canvas_rect(c, EMBED_X, EMBED_Y, EMBED_W, EMBED_H, COL_CARD);
    units = (unsigned) (text_w / (INDIGO_CHAR_WIDTH * POST_TEXT_SCALE));
    if (units == 0) {
        units = 1;
    }
    n = indigo_wrap(title, units, lines, 2, &truncated);

    for (unsigned i = 0; i < n; i++) {
        indigo_canvas_text(c, text_x,
                           EMBED_Y + (float) CARD_INSET +
                               (float) POST_LINE_PITCH * (float) i,
                           POST_TEXT_SCALE, COL_TEXT, "%.*s%s", (int) lines[i].len,
                           title + lines[i].start,
                           truncated && i + 1 == n ? "..." : "");
    }
    /* The title, when there was one, sits above the URI it belongs to. */
    y = EMBED_Y + (float) CARD_INSET + (float) POST_LINE_PITCH * (float) n;
    indigo_canvas_text(c, text_x, y, 0.55f, COL_TEXT_DIM, "%.38s", p->embed_uri);
    if (p->embed_thumb[0]) {
        indigo_canvas_image(c, EMBED_X + EMBED_W - (float) (CARD_INSET + CARD_THUMB),
                            EMBED_Y + (float) (CARD_INSET - 4), CARD_THUMB, CARD_THUMB,
                            p->embed_thumb,
                            indigo_media_placeholder_color(p->embed_thumb));
    }
}

/* The post's text and its embed, filling the space between the header and the
 * counters. An embed that cannot be drawn keeps its one-line note -- a quote, a
 * video, an attachment -- and an embed that can be drawn does not, because the
 * note would say less than the picture does. */
void
indigo_layout_draw_post_body(indigo_canvas *c, const indigo_post *p, bool show_alt,
               unsigned text_scale)
{
    bool embed = post_draws_embed(p);
    float band_h = EMBED_H;

    draw_post_text(c, p, embed ? POST_TEXT_LINES_EMBED : POST_TEXT_LINES, text_scale);
    if (embed) {
        if (p->embed_kind == INDIGO_EMBED_LINK) {
            draw_post_link(c, p);
            return;
        }
        if (p->embed_kind == INDIGO_EMBED_VIDEO) {
            /* The label takes its line off the band before the poster is
             * fitted into what is left, so the two never overlap. */
            band_h = EMBED_H - draw_post_alt(c, "Video: it can't play on the 3DS") - 4.0f;
            draw_post_image(c, p, band_h < EMBED_H_MIN ? EMBED_H_MIN : band_h);
            return;
        }
        if (show_alt && p->embed_alt[0]) {
            /* The alt text sits at the bottom of the band, so the picture is
             * fitted into what is above it. */
            float used = draw_post_alt(c, p->embed_alt);

            band_h = EMBED_H - used - 4.0f;
            if (band_h < EMBED_H_MIN) {
                band_h = EMBED_H_MIN;
            }
        }
        draw_post_image(c, p, band_h);
        return;
    }
    if (p->embed_note[0]) {
        indigo_canvas_text(c, POST_TEXT_X, POST_NOTE_Y, 0.55f, COL_TEXT_DIM, "%s",
                           p->embed_note);
    }
}

void
indigo_layout_build_top_post(const indigo_app *app, indigo_canvas *c)
{
    bool thread = app->screen == INDIGO_SCREEN_THREAD;
    const indigo_timeline *t = thread ? &app->thread : &app->timeline;
    const indigo_post *p = indigo_timeline_selected(t);

    const char *title = thread ? "Thread" : app->feed_uri[0] ? app->feed_name : "Home";
    /* A feed name is longer than "Home", and the counter above the rows wants
     * the right of the bar, so the feed hint drops the START reminder to keep
     * room for the name: START still exits, as it does on every list screen. */
    const char *hint = thread             ? "B  Back   SEL  Profile"
                        : app->feed_uri[0] ? "B  Feeds   SEL  Reload"
                        : "B  Menu   SEL  Reload   START  Exit";
    float hint_x = 18.0f + (float) strlen(title) * INDIGO_CHAR_WIDTH * 0.8f + 12.0f;
    float hint_end = 326.0f - (float) strlen(hint) * INDIGO_CHAR_WIDTH * 0.5f;
    unsigned units;

    if (hint_x < 116.0f) {
        hint_x = 116.0f;
    }
    if (hint_x > hint_end && hint_end > 116.0f) {
        hint_x = hint_end;
    }
    /* A name that would still reach the hint is cut, and marked as cut, rather
     * than drawn over it. */
    units = (unsigned) ((hint_x - 26.0f) / (INDIGO_CHAR_WIDTH * 0.8f));
    if (strlen(title) > units) {
        indigo_canvas_text(c, 18, 8, 0.8f, COL_TEXT, "%.*s...", (int) units - 3, title);
    } else {
        indigo_canvas_text(c, 18, 8, 0.8f, COL_TEXT, "%s", title);
    }
    /* The bottom pills carry the hints for the list actions (Y, X, A, SEL), so
     * the title bar only states what they cannot: leaving the screen. The one
     * exception is a status the bottom screen's header gave up to the viewer
     * button: a failure fetching this list is worth this bar more than the
     * reminder of which key reloads it, and this bar has the room the header
     * spent. */
    if (image_button_shown(app) && t->status[0]) {
        indigo_canvas_text(c, hint_x, 12, 0.5f, t->status_is_error ? COL_ERROR : COL_TEXT_DIM,
                           "%.60s", t->status);
    } else {
        indigo_canvas_text(c, hint_x, 12, 0.5f, COL_TEXT_DIM, "%s", hint);
    }
    if (p) {
        indigo_canvas_text(c, 330, 12, 0.6f, COL_TEXT_DIM, "%u / %u%s", t->selected + 1,
                           t->count, t->has_more ? "+" : "");
    }

    if (!p) {
        indigo_canvas_text(c, 18, 104, 0.8f, COL_TEXT_SOFT, "%s",
                           t->loading ? (thread ? "Loading the thread..." : "Loading your timeline...")
                       : "No posts to show.");
        if (t->status[0]) {
            indigo_canvas_text(c, 18, 136, 0.65f,
                               t->status_is_error ? COL_ERROR : COL_TEXT_DIM, "%s", t->status);
        }
        return;
    }

    if (thread) {
        indigo_canvas_text(c, 18, 36, 0.55f, COL_TEXT_DIM, "%s",
                           t->selected < app->thread_focus    ? "Earlier in the thread"
                           : t->selected == app->thread_focus ? "The post"
                                                               : "A reply");
    } else if (p->reposted_by[0]) {
        indigo_canvas_text(c, 18, 36, 0.55f, COL_REPOSTED, "Reposted by %s", p->reposted_by);
    } else if (p->is_reply) {
        indigo_canvas_text(c, 18, 36, 0.55f, COL_TEXT_DIM, "Reply");
    }
    indigo_layout_draw_avatar(c, p->avatar, 12, 50, HEAD_AVATAR);
    /* The name and handle sit beside the avatar rather than above the text:
     * the text block is a fixed five lines starting at POST_TEXT_Y, and there
     * is no room to give the header a third line. */
    indigo_canvas_text(c, 44, 52, 0.75f, COL_TEXT, "%s", indigo_layout_author_name(p));
    indigo_canvas_text(c, 44, 76, 0.55f, COL_TEXT_DIM, "@%s", p->handle);
    indigo_layout_draw_post_body(c, p, app->settings.alt_text, app->settings.text_scale);

    indigo_canvas_text(c, 18, POST_COUNTER_Y, 0.55f, COL_TEXT_DIM, "%u replies",
                       p->reply_count);
    indigo_canvas_text(c, 118, POST_COUNTER_Y, 0.55f,
                       p->repost_uri[0] ? COL_REPOSTED : COL_TEXT_DIM, "%u reposts",
                       p->repost_count);
    indigo_canvas_text(c, 218, POST_COUNTER_Y, 0.55f,
                       p->like_uri[0] ? COL_LIKED : COL_TEXT_DIM, "%u likes",
                       p->like_count);
    if (t->loading) {
        indigo_canvas_text(c, 330, POST_COUNTER_Y, 0.55f, COL_TEXT_DIM, "Loading...");
    } else if (t->status[0]) {
        indigo_canvas_text(c, 190, 36, 0.55f, t->status_is_error ? COL_ERROR : COL_TEXT_DIM,
                           "%s", t->status);
    }
}
