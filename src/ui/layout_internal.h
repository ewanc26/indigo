#ifndef INDIGO_LAYOUT_INTERNAL_H
#define INDIGO_LAYOUT_INTERNAL_H

/*
 * What the layout's files share and nothing else may use. The public interface
 * is ui/layout.h.
 *
 * layout.c is the geometry: where every control is, the hit tests and the
 * palette. layout_top.c and layout_bottom.c build each screen's two halves;
 * layout_post.c draws a post (the top screen's main job); layout_widgets.c has
 * the pieces every screen reuses (a pill, a list row, a title, a paragraph).
 */

#include "ui/layout.h"

#include "app/signin.h"
#include "atproto/session.h"
#include "media/media.h"
#include "ui/wrap.h"
#include "util/buildinfo.h"

#include <wolfram/attach.h>

#include <stdio.h>
#include <string.h>

#define COL_BG_TOP INDIGO_RGBA(18, 20, 26, 255)
#define COL_BG_BOTTOM INDIGO_RGBA(12, 14, 18, 255)
#define COL_BAR INDIGO_RGBA(30, 34, 44, 255)
#define COL_TEXT INDIGO_RGBA(255, 255, 255, 255)
#define COL_TEXT_SOFT INDIGO_RGBA(220, 224, 232, 255)
#define COL_TEXT_DIM INDIGO_RGBA(160, 168, 184, 255)
#define COL_PILL INDIGO_RGBA(44, 50, 66, 255)
/* A link card is a surface, not a control: one step above the background, so
 * it reads as a card the reader can look into rather than something to press. */
#define COL_CARD INDIGO_RGBA(27, 31, 41, 255)
#define COL_PILL_ACTIVE INDIGO_RGBA(74, 96, 180, 255)
/* The one colour that is not themed: the surround of the full-size viewer. A
 * photograph has to be judged against something neutral, and it is explained
 * where it is drawn. */
#define COL_VIEWER_BG INDIGO_RGBA(8, 8, 10, 255)
/* Timeline-style lists: three rows with 4px between, then a row of four
 * action pills with 6px gaps. Every list screen shares them. */
#define ROW_X 8
#define ROW_W 304
#define ROW_H 46
#define ROW_STEP INDIGO_LIST_ROW_PX
#define ROW_Y0 48
#define PILL_Y 202
#define PILL_W 74
#define PILL_H 34
/* Menu: five full-width items. */
#define MENU_X 20
#define MENU_W 280
#define MENU_H 34
#define MENU_Y0 50
#define MENU_STEP 38
/* 206 + one 0.6-scale text line clears the 240-tall bottom screen. The
 * spacing test below pins that the button rows leave room for it. */
#define PROFILE_STATUS_Y 206
/* Settings rows: eight options on the bottom screen.
 *
 * Eight rows of the original 24px with a 27px step reach 257 on a 240-tall
 * screen, so the rows gave up 2px of height and the first row moved up to clear
 * the Back button by the same margin it always had. The 3px gap is unchanged,
 * because the gap is what decides whether a thumb can land on the wrong row.
 * Making the screen scrollable is the answer that compromises none of this, and
 * it is the answer to reach at a ninth row rather than an eighth. */
#define SETTINGS_ROW_X 14
#define SETTINGS_ROW_W 292
#define SETTINGS_ROW_H 22
#define SETTINGS_ROW_Y0 41
#define SETTINGS_ROW_STEP 25

#define COL_ERROR INDIGO_RGBA(255, 138, 128, 255)

#define COL_LINK INDIGO_RGBA(112, 168, 255, 255)
#define COL_LIKED INDIGO_RGBA(255, 120, 150, 255)
#define COL_REPOSTED INDIGO_RGBA(120, 220, 160, 255)
#define POST_TEXT_SCALE 0.6f
#define POST_TEXT_X 18
#define POST_TEXT_Y 98
#define POST_LINE_PITCH 19
#define POST_TEXT_LINES 5

/* Avatar sizes. The row size is what a 304x46 row can give up without
 * squeezing the two text lines; the header sizes are the top screen's. */
#define ROW_AVATAR 32
#define HEAD_AVATAR 24
/* A post's embed -- an image, or a link card -- takes the space between the
 * text and the counters, so the text drops to POST_TEXT_LINES_EMBED lines.
 *
 * The other way to do it is a thumbnail beside the text, and that is worse: it
 * costs about 130px of width, which is nearly half the characters per line, so
 * a post's worth of text would not fit in the space the thumbnail would have
 * taken. Two lines of caption and a real box for the picture is the better
 * trade on a screen this size. */
#define POST_TEXT_LINES_EMBED 2
#define POST_NOTE_Y 196
#define POST_COUNTER_Y 214
#define EMBED_X 18
#define EMBED_Y 132
#define EMBED_W 364
#define EMBED_H 60
/* Alt text: the scale it is drawn at, and the picture height kept when it has
 * taken lines out of the band. A 4:3 photo at the floor is still 40x30. */
#define ALT_SCALE 0.55f
#define EMBED_H_MIN 30.0f
/* A link card's text sits inside its own edge; the inset is the padding. */
#define CARD_INSET 10
/* The thumbnail inside a link card, and the space kept clear for it. */
#define CARD_THUMB 52
#define PROFILE_AVATAR 40

typedef struct {
    uint32_t bg_top;
    uint32_t bg_bottom;
    uint32_t bar;
    uint32_t text;
    uint32_t text_soft;
    uint32_t text_dim;
    uint32_t pill;
    uint32_t pill_active;
} indigo_palette;

/* layout.c: the colours for the current settings. */
indigo_palette indigo_layout_palette(const indigo_settings *s);

/* layout_widgets.c */
void indigo_layout_top_title(indigo_canvas *c, const char *title, const char *hint);
float indigo_layout_paragraph(indigo_canvas *c, float x, float y, float width, float scale, uint32_t color, unsigned max_lines, const char *text);
float indigo_layout_top_paragraph(indigo_canvas *c, float x, float y, float scale, uint32_t color, unsigned max_lines, const char *text);
void indigo_layout_action_pill(indigo_canvas *c, indigo_action action, bool on, bool busy, uint32_t on_color, const char *label);
void indigo_layout_back_button(indigo_canvas *c, indigo_action action, const char *label);
void indigo_layout_list_row(indigo_canvas *c, indigo_action a, bool selected, const char *title, const char *body, const char *avatar);
void indigo_layout_draw_avatar(indigo_canvas *c, const char *url, float x, float y, float size);
void indigo_layout_field_row(indigo_canvas *c, indigo_action action, const indigo_signin *s, indigo_field f);
void indigo_layout_toggle_button(indigo_canvas *c, indigo_action action, bool enabled, bool busy, const char *on_label, const char *off_label);
unsigned indigo_layout_utf8_length(const char *s);
const char * indigo_layout_compose_title(const indigo_compose *c);
const char * indigo_layout_note_verb(indigo_note_kind k);

/* layout_post.c */
const char * indigo_layout_author_name(const indigo_post *p);
bool indigo_layout_image_button(indigo_canvas *c, const indigo_app *app);
void indigo_layout_draw_post_body(indigo_canvas *c, const indigo_post *p, bool show_alt, unsigned text_scale);
void indigo_layout_build_top_post(const indigo_app *app, indigo_canvas *c);

/* layout_top.c */
void indigo_layout_build_top(const indigo_app *app, indigo_canvas *c);

/* layout_bottom.c */
void indigo_layout_build_bottom(const indigo_app *app, const indigo_input *input, indigo_canvas *c);

#endif
