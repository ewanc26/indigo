/* The pieces every layout screen reuses: a title, a paragraph, a pill, a list row, an avatar. */

#include "ui/layout_internal.h"

/* A square image at `size` pixels, or a tinted placeholder while it loads.
 *
 * The tint comes from the URL, so the same account is the same colour
 * everywhere it appears and two accounts never look alike: a column of
 * identical grey squares reads as one voice, which is the wrong thing for a
 * timeline to say. It is the only colour on screen not drawn from the palette,
 * and that is deliberate -- it has to be distinguishable per account. */
void
indigo_layout_draw_avatar(indigo_canvas *c, const char *url, float x, float y, float size)
{
    if (!url || !url[0]) {
        return;
    }
    indigo_canvas_image(c, x, y, size, size, url, indigo_media_placeholder_color(url));
}

void
indigo_layout_top_title(indigo_canvas *c, const char *title, const char *hint)
{
    indigo_canvas_rect(c, 0, 0, INDIGO_TOP_WIDTH, 32, COL_BAR);
    indigo_canvas_text(c, 18, 8, 0.8f, COL_TEXT, "%s", title);
    indigo_canvas_text(c, 190, 12, 0.5f, COL_TEXT_DIM, "%s", hint);
}

/* Wrap `text` into the top screen at (x, y); returns the next free y. */
float
indigo_layout_paragraph(indigo_canvas *c, float x, float y, float width, float scale, uint32_t color,
           unsigned max_lines, const char *text)
{
    indigo_line lines[8];
    int truncated;
    unsigned units = (unsigned) ((width - 2 * x) / (INDIGO_CHAR_WIDTH * scale));
    unsigned n;

    if (max_lines > 8) {
        max_lines = 8;
    }
    n = indigo_wrap(text, units, lines, max_lines, &truncated);
    for (unsigned i = 0; i < n; i++) {
        indigo_canvas_text(c, x, y, scale, color, "%.*s%s", (int) lines[i].len,
                           text + lines[i].start, truncated && i + 1 == n ? "..." : "");
        y += (float) POST_LINE_PITCH * scale / POST_TEXT_SCALE * 0.95f;
    }
    return y;
}

/* The top screen's own width, which is what every other paragraph on it wraps
 * to. The viewer is the one place a paragraph goes on the bottom screen. */
float
indigo_layout_top_paragraph(indigo_canvas *c, float x, float y, float scale, uint32_t color,
              unsigned max_lines, const char *text)
{
    return indigo_layout_paragraph(c, x, y, (float) INDIGO_TOP_WIDTH, scale, color, max_lines, text);
}

const char *
indigo_layout_note_verb(indigo_note_kind k)
{
    switch (k) {
    case INDIGO_NOTE_LIKE:
        return "liked your post";
    case INDIGO_NOTE_REPOST:
        return "reposted your post";
    case INDIGO_NOTE_FOLLOW:
        return "followed you";
    case INDIGO_NOTE_REPLY:
        return "replied to you";
    case INDIGO_NOTE_MENTION:
        return "mentioned you";
    case INDIGO_NOTE_QUOTE:
        return "quoted your post";
    case INDIGO_NOTE_OTHER:
        break;
    }
    return "did something";
}

unsigned
indigo_layout_utf8_length(const char *s)
{
    unsigned n = 0;

    for (; *s; s++) {
        if (((unsigned char) *s & 0xC0) != 0x80) {
            n++;
        }
    }
    return n;
}

const char *
indigo_layout_compose_title(const indigo_compose *c)
{
    return c->mode == INDIGO_COMPOSE_REPLY   ? "Reply"
           : c->mode == INDIGO_COMPOSE_QUOTE ? "Quote post"
                                             : "New post";
}

void
indigo_layout_field_row(indigo_canvas *c, indigo_action action, const indigo_signin *s,
          indigo_field f)
{
    indigo_rect r = indigo_layout_button_rect(action);
    char shown[64];

    indigo_signin_display(s, f, shown, sizeof shown);
    indigo_canvas_rect(c, r.x, r.y, r.w, r.h, s->focus == f ? COL_PILL_ACTIVE : COL_PILL);
    indigo_canvas_text(c, r.x + 8, r.y + 2, 0.5f, COL_TEXT_SOFT, "%s",
                       indigo_signin_field_label(f));
    indigo_canvas_text(c, r.x + 8, r.y + 16, 0.65f, COL_TEXT, "%s",
                       shown[0] ? shown : "Tap to enter");
}

void
indigo_layout_action_pill(indigo_canvas *c, indigo_action action, bool on, bool busy, uint32_t on_color,
            const char *label)
{
    indigo_rect r = indigo_layout_button_rect(action);

    indigo_canvas_rect(c, r.x, r.y, r.w, r.h, on ? on_color : COL_PILL);
    indigo_canvas_text(c, r.x + 8, r.y + 9, 0.55f, busy ? COL_TEXT_DIM : COL_TEXT, "%s",
                       label);
}

void
indigo_layout_back_button(indigo_canvas *c, indigo_action action, const char *label)
{
    indigo_rect r = indigo_layout_button_rect(action);

    indigo_canvas_rect(c, r.x, r.y, r.w, r.h, COL_PILL);
    indigo_canvas_text(c, r.x + 10, r.y + 7, 0.6f, COL_TEXT, "%s", label);
}

/* One list row: a pill, a title line, a body line and, when there is one, the
 * author's avatar. The avatar is drawn into every kind of row rather than each
 * screen doing its own, so a person looks the same in the timeline, in search
 * results, in a list of followers and in notifications. */
void
indigo_layout_list_row(indigo_canvas *c, indigo_action a, bool selected, const char *title,
         const char *body, const char *avatar)
{
    indigo_rect r = indigo_layout_button_rect(a);
    float text_x = r.x + 10;
    unsigned units;
    indigo_line line;
    int cut;

    indigo_canvas_rect(c, r.x, r.y, r.w, r.h, selected ? COL_PILL_ACTIVE : COL_PILL);
    if (avatar && avatar[0]) {
        indigo_layout_draw_avatar(c, avatar, r.x + 7, r.y + 7, ROW_AVATAR);
        text_x = r.x + 7 + ROW_AVATAR + 8;
    }
    /* The wrap width follows the text's left edge, so an indented row is
     * truncated as tightly as an unindented one rather than running under the
     * row's right edge. */
    units = (unsigned) ((r.x + ROW_W - 10 - text_x) / (INDIGO_CHAR_WIDTH * 0.55f));
    indigo_canvas_text(c, text_x, r.y + 3, 0.6f, COL_TEXT, "%s", title);
    if (indigo_wrap(body, units, &line, 1, &cut) == 0) {
        line = (indigo_line) {0, 0};
    }
    indigo_canvas_text(c, text_x, r.y + 24, 0.55f, COL_TEXT_SOFT, "%.*s%s", (int) line.len,
                       body + line.start, cut ? "..." : "");
}

/* A toggle pair: the button names the action it performs, not the state it is
 * in, so the label never has to change under the reader. */
void
indigo_layout_toggle_button(indigo_canvas *c, indigo_action action, bool enabled, bool busy,
              const char *on_label, const char *off_label)
{
    indigo_rect r = indigo_layout_button_rect(action);

    indigo_canvas_rect(c, r.x, r.y, r.w, r.h, enabled && !busy ? COL_PILL_ACTIVE : COL_PILL);
    indigo_canvas_text(c, r.x + 10, r.y + 11, 0.6f, enabled && !busy ? COL_TEXT : COL_TEXT_DIM,
                       "%s", busy ? "..." : (enabled ? off_label : on_label));
}
