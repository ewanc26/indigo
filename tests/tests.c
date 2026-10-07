#include "app/app.h"
#include "app/signin.h"
#include "app/timeline.h"
#include "atproto/errors.h"
#include "atproto/prefs.h"
#include "media/media.h"
#include "store/session_codec.h"
#include "util/log.h"
#include "store/session_store.h"
#include "store/settings_codec.h"
#include "store/draft_store.h"
#include "store/file.h"
#include "store/settings_store.h"
#include "gfx/canvas.h"
#include "input/input.h"
#include "ui/layout.h"
#include "ui/wrap.h"
#include "util/buildinfo.h"
#include "util/clock.h"
#include "update/update.h"
#include "update/update_sig.h"
#include "media/cdn_url.h"
#include "update/updater.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <wolfram/profile_tab.h>

static int s_checks;
static int s_failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        s_checks++;                                                              \
        if (!(cond)) {                                                           \
            s_failures++;                                                        \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

static void
test_canvas_basics(void)
{
    indigo_canvas c;

    indigo_canvas_init(&c, 100, 50);
    CHECK(indigo_canvas_rect(&c, 1, 2, 3, 4, 0));
    CHECK(indigo_canvas_text(&c, 5, 6, 1.0f, 0, "hi %d", 42));
    CHECK(c.count == 2);
    CHECK(strcmp(indigo_canvas_cmd_text(&c, &c.cmds[1]), "hi 42") == 0);
    CHECK(!c.overflow);
}

static void
test_canvas_overflow_is_bounded(void)
{
    indigo_canvas c;
    int accepted = 0;

    indigo_canvas_init(&c, 10, 10);

    for (int i = 0; i < INDIGO_CANVAS_MAX_CMDS + 10; i++) {
        accepted += indigo_canvas_rect(&c, 0, 0, 1, 1, 0) ? 1 : 0;
    }

    CHECK(accepted == INDIGO_CANVAS_MAX_CMDS);
    CHECK(c.overflow);

    indigo_canvas_init(&c, 10, 10);
    char big[INDIGO_CANVAS_TEXT_BYTES + 8];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    CHECK(!indigo_canvas_text(&c, 0, 0, 1.0f, 0, "%s", big));
    CHECK(c.overflow);
    CHECK(c.count == 0);
}

static void
test_app_navigation(void)
{
    indigo_app app;
    indigo_input in = {0};

    indigo_app_init(&app);
    CHECK(app.screen == INDIGO_SCREEN_SIGNIN);
    app.screen = INDIGO_SCREEN_HOME;

    /* Nothing is selected on an empty timeline, so A does nothing. */
    in.confirm = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_HOME);

    in = (indigo_input) {0};
    in.back = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_MENU);

    in = (indigo_input) {0};
    in.back = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_HOME);

    in = (indigo_input) {0};
    CHECK(!indigo_app_should_quit(&app));
    in.quit = true;
    indigo_app_update(&app, &in);
    CHECK(indigo_app_should_quit(&app));
}

static void
test_touch_navigation(void)
{
    indigo_app app;
    indigo_input in = {0};
    indigo_rect menu = indigo_layout_button_rect(INDIGO_ACTION_MENU);
    indigo_rect back = indigo_layout_button_rect(INDIGO_ACTION_BACK);

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_HOME;
    in.touch_pressed = true;
    in.touch_x = (int) (menu.x + menu.w / 2);
    in.touch_y = (int) (menu.y + menu.h / 2);
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_MENU);

    in.touch_x = (int) (back.x + back.w / 2);
    in.touch_y = (int) (back.y + back.h / 2);
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_HOME);

    /* A held touch must not re-trigger. */
    app.screen = INDIGO_SCREEN_PROFILE;
    app.history[0] = INDIGO_SCREEN_HOME;
    app.history_count = 1;
    in.touch_pressed = false;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_PROFILE);
}

static bool
overlap(indigo_rect a, indigo_rect b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static void
test_buttons_spaced_and_on_screen(void)
{
    static const indigo_action home_ui[] = {
        INDIGO_ACTION_ROW0, INDIGO_ACTION_ROW1, INDIGO_ACTION_ROW2,
        INDIGO_ACTION_LIKE, INDIGO_ACTION_REPOST, INDIGO_ACTION_OPEN, INDIGO_ACTION_REFRESH,
        INDIGO_ACTION_MENU};
    static const indigo_action pills[] = {
        INDIGO_ACTION_LIKE, INDIGO_ACTION_REPOST, INDIGO_ACTION_OPEN, INDIGO_ACTION_REFRESH};
    enum { N = sizeof home_ui / sizeof home_ui[0], P = sizeof pills / sizeof pills[0] };

    for (int i = 0; i < N; i++) {
        indigo_rect r = indigo_layout_button_rect(home_ui[i]);

        CHECK(r.x >= 0 && r.x + r.w <= INDIGO_BOTTOM_WIDTH);
        CHECK(r.y >= 0 && r.y + r.h <= INDIGO_BOTTOM_HEIGHT);
        CHECK(r.h >= 34.0f);
        for (int j = i + 1; j < N; j++) {
            CHECK(!overlap(r, indigo_layout_button_rect(home_ui[j])));
        }
        CHECK(indigo_layout_hit(INDIGO_SCREEN_HOME, (int) (r.x + r.w / 2),
                                (int) (r.y + r.h / 2)) == home_ui[i]);
    }

    /* Pills are 6px apart; the gap between them is dead space. */
    for (int i = 0; i + 1 < P; i++) {
        indigo_rect a = indigo_layout_button_rect(pills[i]);
        indigo_rect b = indigo_layout_button_rect(pills[i + 1]);

        CHECK(b.x - (a.x + a.w) >= 6.0f);
        CHECK(indigo_layout_hit(INDIGO_SCREEN_HOME, (int) (a.x + a.w + 3), (int) a.y + 5) ==
              INDIGO_ACTION_NONE);
    }
    /* The profile stacks five rows of buttons and still has to leave room for
     * a status line. Asserting only that each rect is on screen would have
     * missed a status line pushed past the bottom edge. */
    {
        static const indigo_action prof[] = {
            INDIGO_ACTION_FOLLOW, INDIGO_ACTION_MUTE, INDIGO_ACTION_BLOCK,
            INDIGO_ACTION_FOLLOWERS, INDIGO_ACTION_FOLLOWING, INDIGO_ACTION_POSTS,
            INDIGO_ACTION_PINNED, INDIGO_ACTION_BACK};
        float lowest = 0.0f;

        for (int i = 0; i < (int) (sizeof prof / sizeof prof[0]); i++) {
            indigo_rect r = indigo_layout_button_rect(prof[i]);

            CHECK(r.x >= 0 && r.x + r.w <= INDIGO_BOTTOM_WIDTH);
            CHECK(r.y >= 0 && r.y + r.h <= INDIGO_BOTTOM_HEIGHT);
            CHECK(r.h >= 34.0f);
            for (int j = i + 1; j < (int) (sizeof prof / sizeof prof[0]); j++) {
                CHECK(!overlap(r, indigo_layout_button_rect(prof[j])));
            }
            if (r.y + r.h > lowest) {
                lowest = r.y + r.h;
            }
        }
        /* Back sits in the header bar, so the row-based buttons end at Posts. */
        float rows_lowest = 0.0f;

        for (int i = 0; i < 7; i++) {
            indigo_rect r = indigo_layout_button_rect(prof[i]);

            if (r.y + r.h > rows_lowest) {
                rows_lowest = r.y + r.h;
            }
        }
        /* One 0.6-scale text line plus a little clearance. A status line
         * pushed past the bottom edge is invisible, which is how the last
         * version of this screen lost its error messages. */
        CHECK(rows_lowest + 20.0f <= INDIGO_BOTTOM_HEIGHT);
    }

    CHECK(indigo_layout_hit(INDIGO_SCREEN_HOME, 0, 0) == INDIGO_ACTION_NONE);
    CHECK(indigo_layout_hit(INDIGO_SCREEN_PROFILE, 160, 20) == INDIGO_ACTION_NONE);
    CHECK(indigo_layout_hit(INDIGO_SCREEN_MENU, 160, 20) == INDIGO_ACTION_NONE);
}

static unsigned
count_text(const indigo_canvas *c, const char *needle)
{
    unsigned n = 0;

    for (unsigned i = 0; i < c->count; i++) {
        if (c->cmds[i].kind == INDIGO_CMD_TEXT &&
            strstr(indigo_canvas_cmd_text(c, &c->cmds[i]), needle)) {
            n++;
        }
    }

    return n;
}

static void
test_layout_invariants(void)
{
    static indigo_canvas top;
    static indigo_canvas bottom;

    for (int screen = 0; screen <= INDIGO_SCREEN_SEARCH; screen++) {
        indigo_app app;
        indigo_input in = {0};

        indigo_app_init(&app);
        app.screen = (indigo_screen) screen;
        indigo_layout_build(&app, &in, &top, &bottom);

        CHECK(top.width == 400 && top.height == 240);
        CHECK(bottom.width == 320 && bottom.height == 240);
        CHECK(!top.overflow && !bottom.overflow);

        const indigo_canvas *both[] = {&top, &bottom};

        for (int k = 0; k < 2; k++) {
            for (unsigned i = 0; i < both[k]->count; i++) {
                const indigo_cmd *cmd = &both[k]->cmds[i];

                if (cmd->kind == INDIGO_CMD_RECT) {
                    CHECK(cmd->x >= 0 && cmd->y >= 0);
                    CHECK(cmd->x + cmd->w <= (float) both[k]->width);
                    CHECK(cmd->y + cmd->h <= (float) both[k]->height);
                } else {
                    CHECK(cmd->x >= 0 && cmd->y >= 0);
                    CHECK(cmd->x < (float) both[k]->width && cmd->y < (float) both[k]->height);
                }
            }
        }

        /* START is hinted exactly where it exits: sign-in, Home and the menu. */
        CHECK(count_text(&top, "START") + count_text(&bottom, "START") ==
              (screen == INDIGO_SCREEN_SIGNIN || screen == INDIGO_SCREEN_HOME ||
               screen == INDIGO_SCREEN_MENU ? 1u : 0u));
    }
}

static indigo_post
make_post(const char *uri, const char *text)
{
    indigo_post p = {0};

    indigo_copy_utf8(p.uri, sizeof p.uri, uri);
    indigo_copy_utf8(p.cid, sizeof p.cid, "bafycid");
    indigo_copy_utf8(p.handle, sizeof p.handle, "rhi.example.social");
    indigo_copy_utf8(p.text, sizeof p.text, text);
    return p;
}

static void
test_drag_scrolls_feeds(void)
{
    indigo_app app;
    indigo_input in = {0};
    indigo_rect row = indigo_layout_button_rect(INDIGO_ACTION_ROW0);
    indigo_rect like = indigo_layout_button_rect(INDIGO_ACTION_LIKE);
    indigo_post p;
    char uri[48];

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_HOME;
    for (int i = 0; i < 8; i++) {
        snprintf(uri, sizeof uri, "at://a/app.bsky.feed.post/%d", i);
        p = make_post(uri, "post");
        indigo_timeline_append(&app.timeline, &p);
    }
    CHECK(app.timeline.selected == 0);

    /* A drag that began on a row moves the selection by the rows it covered: up is
     * forward, down is back, and it stops at the ends. */
    in.drag_start_x = (int) (row.x + row.w / 2);
    in.drag_start_y = (int) (row.y + row.h / 2);
    in.drag_rows = 2;
    indigo_app_update(&app, &in);
    CHECK(app.timeline.selected == 2);
    in.drag_rows = -1;
    indigo_app_update(&app, &in);
    CHECK(app.timeline.selected == 1);
    in.drag_rows = 50;
    indigo_app_update(&app, &in);
    CHECK(app.timeline.selected == 7);
    in.drag_rows = -50;
    indigo_app_update(&app, &in);
    CHECK(app.timeline.selected == 0);

    /* A frame with no movement, and a drag that began on a button, move nothing. */
    in.drag_rows = 0;
    indigo_app_update(&app, &in);
    CHECK(app.timeline.selected == 0);
    in.drag_start_x = (int) (like.x + like.w / 2);
    in.drag_start_y = (int) (like.y + like.h / 2);
    in.drag_rows = 3;
    indigo_app_update(&app, &in);
    CHECK(app.timeline.selected == 0);
}

static void
test_thread_navigation(void)
{
    indigo_app app;
    indigo_input in = {0};
    indigo_field f;
    indigo_post posts[3];

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_HOME;
    posts[0] = make_post("at://a/app.bsky.feed.post/1", "one");
    indigo_timeline_append(&app.timeline, &posts[0]);

    in.confirm = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_THREAD);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_THREAD);
    CHECK(strcmp(app.request_post_uri, "at://a/app.bsky.feed.post/1") == 0);

    posts[1] = make_post("at://a/app.bsky.feed.post/0", "parent");
    posts[2] = make_post("at://a/app.bsky.feed.post/2", "reply");
    {
        indigo_post list[3] = {posts[1], posts[0], posts[2]};

        indigo_app_thread_loaded(&app, list, 3, 1);
    }
    CHECK(app.thread.count == 3);
    CHECK(app.thread.selected == 1);

    /* Like on the thread updates the timeline copy too. */
    in = (indigo_input) {0};
    in.like = true;
    indigo_app_update(&app, &in);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_LIKE);
    CHECK(app.thread.posts[1].like_pending);
    CHECK(app.timeline.posts[0].like_pending);

    /* B returns to where the thread was opened from. */
    in = (indigo_input) {0};
    in.back = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_HOME);
}

static void
test_compose_reply_gate(void)
{
    indigo_compose c = {0};

    c.mode = INDIGO_COMPOSE_POST;

    /* Everyone is the default, and no threadgate at all already means it. */
    CHECK(c.reply_gate == INDIGO_REPLY_GATE_EVERYONE);
    CHECK(strcmp(indigo_compose_gate_label(&c), "Everyone") == 0);
    CHECK(strcmp(indigo_compose_gate_short(&c), "Everyone") == 0);
    CHECK(indigo_compose_can_gate(&c));

    /* The choice cycles and wraps back round. */
    indigo_compose_gate_cycle(&c);
    CHECK(c.reply_gate == INDIGO_REPLY_GATE_FOLLOWED_MENTIONED);
    CHECK(strcmp(indigo_compose_gate_label(&c), "People you follow and mention") == 0);
    CHECK(strcmp(indigo_compose_gate_short(&c), "Follows + mentions") == 0);

    indigo_compose_gate_cycle(&c);
    CHECK(c.reply_gate == INDIGO_REPLY_GATE_NOBODY);
    CHECK(strcmp(indigo_compose_gate_label(&c), "Nobody") == 0);
    CHECK(strcmp(indigo_compose_gate_short(&c), "Nobody") == 0);

    indigo_compose_gate_cycle(&c);
    CHECK(c.reply_gate == INDIGO_REPLY_GATE_EVERYONE);

    /* Mid-send the choice is frozen, as the reply/quote switch is, but the
     * pill keeps showing it rather than snapping to "Plain post" under the
     * thumb that just hit Post. */
    c.sending = true;
    CHECK(indigo_compose_can_gate(&c));
    c.reply_gate = INDIGO_REPLY_GATE_NOBODY;
    indigo_compose_gate_cycle(&c);
    CHECK(c.reply_gate == INDIGO_REPLY_GATE_NOBODY);
    c.sending = false;

    /* Neither a reply nor a quote is offered the choice. */
    c.has_target = true;
    c.mode = INDIGO_COMPOSE_REPLY;
    CHECK(!indigo_compose_can_gate(&c));
    indigo_compose_gate_cycle(&c);
    CHECK(c.reply_gate == INDIGO_REPLY_GATE_NOBODY);

    c.mode = INDIGO_COMPOSE_QUOTE;
    CHECK(!indigo_compose_can_gate(&c));
    indigo_compose_gate_cycle(&c);
    CHECK(c.reply_gate == INDIGO_REPLY_GATE_NOBODY);
}

static void
test_new_post_reply_gate(void)
{
    indigo_app app;
    indigo_input in = {0};

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_HOME;

    /* B on Home opens the menu, and Compose is the first thing in it. */
    in.back = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_MENU);

    in = (indigo_input) {0};
    in.confirm = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_COMPOSE);
    CHECK(app.compose.mode == INDIGO_COMPOSE_POST);
    CHECK(!app.compose.has_target);

    /* With no target there is no reply/quote to switch, so Y picks the gate. */
    CHECK(app.compose.reply_gate == INDIGO_REPLY_GATE_EVERYONE);

    in = (indigo_input) {0};
    in.like = true;
    indigo_app_update(&app, &in);
    CHECK(app.compose.reply_gate == INDIGO_REPLY_GATE_FOLLOWED_MENTIONED);
    indigo_app_update(&app, &in);
    CHECK(app.compose.reply_gate == INDIGO_REPLY_GATE_NOBODY);
    indigo_app_update(&app, &in);
    CHECK(app.compose.reply_gate == INDIGO_REPLY_GATE_EVERYONE);

    /* The pill under the draft is that same control, by touch. */
    in = (indigo_input) {0};
    in.touch_pressed = true;
    in.touch_x = 160;
    in.touch_y = 162;
    indigo_app_update(&app, &in);
    CHECK(app.compose.reply_gate == INDIGO_REPLY_GATE_FOLLOWED_MENTIONED);

    /* A reply keeps Y on the reply/quote switch and leaves the gate alone. */
    in.back = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_HOME);
}

/* Advance width of one glyph, as wrap.h prices it for line breaking. */
static unsigned
advance_count(const char *s)
{
    unsigned n = 0;

    for (; *s; s++) {
        if (((unsigned char) *s & 0xC0) != 0x80) {
            n++;
        }
    }
    return n;
}

/* The reply gate's three wordings all have to fit the compose pill, and the
 * longest is easy to lengthen by accident, so check every one of them. */
/* Attaching an image: the picker is the menu screen filled with the folder's
 * pictures, choosing one asks for alt text, and the same control takes it off.
 * Which files count (type, size, dotfiles) is Wolfram's and tested there; here,
 * that Indigo wires the folder, the compose state and the request. */
/* The file operations every store shares. */
static void
test_store_file(void)
{
    const char *path = "build-host/file-test.dat";
    char bad[64];
    char buf[16];
    size_t n = 99;
    FILE *f;

    snprintf(bad, sizeof bad, "%s.bad", path);
    mkdir("build-host", 0777);
    indigo_file_remove(path);
    indigo_file_remove(bad);

    CHECK(indigo_file_read(path, buf, sizeof buf, &n) == INDIGO_STORE_MISSING && n == 0);
    CHECK(indigo_file_remove(path) == INDIGO_STORE_OK);

    CHECK(indigo_file_write_atomic(path, "hello", 5) == INDIGO_STORE_OK);
    CHECK(indigo_file_read(path, buf, sizeof buf, &n) == INDIGO_STORE_OK);
    CHECK(n == 5 && memcmp(buf, "hello", 5) == 0);

    /* A second write replaces the first and leaves no temporary file behind. */
    CHECK(indigo_file_write_atomic(path, "abc", 3) == INDIGO_STORE_OK);
    CHECK(indigo_file_read(path, buf, sizeof buf, &n) == INDIGO_STORE_OK && n == 3);
    f = fopen("build-host/file-test.dat.tmp", "rb");
    CHECK(f == NULL);
    if (f) {
        fclose(f);
    }

    /* A file longer than the buffer comes back as exactly the buffer. */
    CHECK(indigo_file_write_atomic(path, "0123456789", 10) == INDIGO_STORE_OK);
    CHECK(indigo_file_read(path, buf, 4, &n) == INDIGO_STORE_OK && n == 4);

    /* A damaged file moves aside, replacing an earlier one. */
    indigo_file_set_aside(path);
    f = fopen(path, "rb");
    CHECK(f == NULL);
    if (f) {
        fclose(f);
    }
    CHECK(indigo_file_read(bad, buf, sizeof buf, &n) == INDIGO_STORE_OK && n == 10);
    CHECK(indigo_file_write_atomic(path, "x", 1) == INDIGO_STORE_OK);
    indigo_file_set_aside(path);
    CHECK(indigo_file_read(bad, buf, sizeof buf, &n) == INDIGO_STORE_OK && n == 1);

    /* Bad arguments are an I/O error, not a crash. */
    CHECK(indigo_file_write_atomic(NULL, "x", 1) == INDIGO_STORE_IO);
    CHECK(indigo_file_write_atomic(path, NULL, 1) == INDIGO_STORE_IO);
    CHECK(indigo_file_read(path, NULL, 4, &n) == INDIGO_STORE_IO);
    indigo_file_set_aside(NULL);

    indigo_file_remove(bad);
}

static void
test_attach_image(void)
{
    char dir[] = "build-host/attach-test";
    char path[256];
    indigo_app app;
    indigo_input in = {0};

    mkdir("build-host", 0777);
    mkdir(dir, 0777);
    snprintf(path, sizeof path, "%s/b.png", dir);
    FILE *f = fopen(path, "wb");
    if (f) {
        fputs("png", f);
        fclose(f);
    }
    snprintf(path, sizeof path, "%s/a.jpg", dir);
    f = fopen(path, "wb");
    if (f) {
        fputs("jpg", f);
        fclose(f);
    }
    snprintf(path, sizeof path, "%s/notes.txt", dir);
    f = fopen(path, "wb");
    if (f) {
        fputs("x", f);
        fclose(f);
    }

    indigo_app_init(&app);
    indigo_copy_utf8(app.images_dir, sizeof app.images_dir, dir);
    app.screen = INDIGO_SCREEN_HOME;
    indigo_copy_utf8(app.compose.text, sizeof app.compose.text, "a post with a picture");
    app.screen = INDIGO_SCREEN_COMPOSE;
    app.compose.mode = INDIGO_COMPOSE_POST;
    CHECK(!indigo_compose_has_image(&app.compose));

    /* Select opens the picker: two pictures in name order, then Close. */
    in.refresh = true;
    indigo_app_update(&app, &in);
    in.refresh = false;
    CHECK(app.screen == INDIGO_SCREEN_MENU);
    CHECK(app.menu.picking_image);
    CHECK(strcmp(app.menu.title, "Images") == 0);
    CHECK(app.menu.count == 3);
    CHECK(strcmp(app.menu.items[0].label, "a.jpg") == 0);
    CHECK(strcmp(app.menu.items[1].label, "b.png") == 0);
    CHECK(app.menu.items[2].kind == INDIGO_MENU_CLOSE);

    /* Choosing the second one attaches it and asks for alt text. */
    in.down = true;
    indigo_app_update(&app, &in);
    in.down = false;
    in.confirm = true;
    indigo_app_update(&app, &in);
    in.confirm = false;
    CHECK(app.screen == INDIGO_SCREEN_COMPOSE);
    CHECK(indigo_compose_has_image(&app.compose));
    CHECK(strcmp(indigo_compose_image_name(&app.compose), "b.png") == 0);
    CHECK(indigo_app_peek_request(&app) == INDIGO_REQUEST_EDIT_IMAGE_ALT);
    indigo_app_take_request(&app, NULL);
    indigo_app_set_image_alt(&app, "a small test picture");
    CHECK(strcmp(app.compose.image_alt, "a small test picture") == 0);

    /* The controls draw, and the same one takes the picture off again. */
    {
        indigo_canvas top;
        indigo_canvas bot;

        indigo_layout_build(&app, &in, &top, &bot);
        CHECK(!top.overflow && !bot.overflow);
    }
    in.refresh = true;
    indigo_app_update(&app, &in);
    in.refresh = false;
    CHECK(app.screen == INDIGO_SCREEN_COMPOSE);
    CHECK(!indigo_compose_has_image(&app.compose));
    CHECK(app.compose.image_alt[0] == '\0');

    /* An empty folder still has a way out. */
    snprintf(path, sizeof path, "%s/a.jpg", dir);
    remove(path);
    snprintf(path, sizeof path, "%s/b.png", dir);
    remove(path);
    in.refresh = true;
    indigo_app_update(&app, &in);
    in.refresh = false;
    CHECK(app.menu.count == 1 && app.menu.items[0].kind == INDIGO_MENU_CLOSE);
    in.confirm = true;
    indigo_app_update(&app, &in);
    in.confirm = false;
    CHECK(app.screen == INDIGO_SCREEN_COMPOSE);

    /* A published post forgets the picture so the next one does not carry it. */
    indigo_copy_utf8(app.compose.image, sizeof app.compose.image, "/x/y.png");
    app.compose.sending = true;
    indigo_app_publish_done(&app);
    CHECK(!indigo_compose_has_image(&app.compose));

    snprintf(path, sizeof path, "%s/notes.txt", dir);
    remove(path);
    remove(dir);
}

static void
test_compose_gate_text_fits(void)
{
    static const char *drafts[] = {
        "",
        "A draft that runs long enough to fill every line the compose box allows, "
        "so the gate line below it is pushed as far down as it will go.",
    };
    indigo_app app;
    indigo_input in = {0};

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_COMPOSE;
    app.compose.mode = INDIGO_COMPOSE_POST;

    for (unsigned d = 0; d < sizeof drafts / sizeof drafts[0]; d++) {
        indigo_copy_utf8(app.compose.text, sizeof app.compose.text, drafts[d]);

        for (unsigned g = 0; g < (unsigned) INDIGO_REPLY_GATE_COUNT; g++) {
            indigo_canvas top;
            indigo_canvas bot;
            const indigo_canvas *screens[2];

            app.compose.reply_gate = (indigo_reply_gate) g;
            indigo_layout_build(&app, &in, &top, &bot);
            screens[0] = &top;
            screens[1] = &bot;

            for (unsigned s = 0; s < 2; s++) {
                const indigo_canvas *c = screens[s];

                CHECK(!c->overflow);
                for (unsigned i = 0; i < c->count; i++) {
                    const indigo_cmd *cmd = &c->cmds[i];
                    double right;

                    if (cmd->kind != INDIGO_CMD_TEXT) {
                        continue;
                    }
                    right = (double) cmd->x +
                            (double) advance_count(indigo_canvas_cmd_text(c, cmd)) *
                                (double) INDIGO_CHAR_WIDTH * (double) cmd->scale;
                    /* No text may run off the edge of its own screen. */
                    CHECK(right <= (double) c->width);
                }
            }
        }
    }
}

static void
test_compose_flow(void)
{
    indigo_app app;
    indigo_input in = {0};
    indigo_field f;
    indigo_post target = make_post("at://a/app.bsky.feed.post/1", "hello");

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_THREAD;
    indigo_timeline_append(&app.thread, &target);
    indigo_copy_utf8(app.thread_uri, sizeof app.thread_uri, target.uri);

    in.confirm = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_COMPOSE);
    CHECK(app.compose.mode == INDIGO_COMPOSE_REPLY);
    CHECK(app.compose.has_target);

    /* Nothing to send until there is text. */
    in = (indigo_input) {0};
    in.repost = true;
    indigo_app_update(&app, &in);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_NONE);

    indigo_app_set_draft(&app, "my reply");
    in.like = true;
    in.repost = false;
    indigo_app_update(&app, &in);
    CHECK(app.compose.mode == INDIGO_COMPOSE_QUOTE);
    indigo_app_update(&app, &in);
    CHECK(app.compose.mode == INDIGO_COMPOSE_REPLY);

    /* Cancelling keeps the draft. */
    in = (indigo_input) {0};
    in.back = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_THREAD);
    CHECK(strcmp(app.compose.text, "my reply") == 0);

    in = (indigo_input) {0};
    in.confirm = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_COMPOSE);
    in = (indigo_input) {0};
    in.repost = true;
    indigo_app_update(&app, &in);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_PUBLISH);
    CHECK(app.compose.sending);

    /* A failure keeps the draft and the screen. */
    indigo_app_publish_failed(&app, "Could not reach the network.");
    CHECK(!app.compose.sending);
    CHECK(app.compose.status_is_error);
    CHECK(strcmp(app.compose.text, "my reply") == 0);
    CHECK(app.screen == INDIGO_SCREEN_COMPOSE);

    /* Success clears the draft and leaves compose. */
    app.compose.sending = true;
    indigo_app_publish_done(&app);
    CHECK(app.compose.text[0] == '\0');
    CHECK(app.screen == INDIGO_SCREEN_THREAD);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_THREAD);
}

static void
test_notifications(void)
{
    indigo_app app;
    indigo_input in = {0};
    indigo_field f;
    indigo_notification items[2] = {0};

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_HOME;
    in.back = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_MENU);
    in = (indigo_input) {0};
    in.down = true;
    indigo_app_update(&app, &in);
    in = (indigo_input) {0};
    in.confirm = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_NOTIFICATIONS);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_NOTIFICATIONS);

    items[0].kind = INDIGO_NOTE_FOLLOW;
    indigo_copy_utf8(items[0].handle, sizeof items[0].handle, "rhi.example.social");
    items[1].kind = INDIGO_NOTE_REPLY;
    indigo_copy_utf8(items[1].target_uri, sizeof items[1].target_uri, "at://a/app.bsky.feed.post/9");
    indigo_app_notifications_loaded(&app, items, 2);
    CHECK(app.notifications.count == 2);

    in = (indigo_input) {0};
    in.down = true;
    indigo_app_update(&app, &in);
    in = (indigo_input) {0};
    in.confirm = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_THREAD);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_THREAD);

    in = (indigo_input) {0};
    in.back = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_NOTIFICATIONS);
}

static void
test_normalise_service(void)
{
    char out[INDIGO_SERVICE_MAX];

    CHECK(indigo_normalise_service("https://bsky.social", out, sizeof out) == INDIGO_INPUT_OK);
    CHECK(strcmp(out, "https://bsky.social") == 0);
    CHECK(indigo_normalise_service("  bsky.social/  ", out, sizeof out) == INDIGO_INPUT_OK);
    CHECK(strcmp(out, "https://bsky.social") == 0);
    CHECK(indigo_normalise_service("pds.example.org", out, sizeof out) == INDIGO_INPUT_OK);
    CHECK(strcmp(out, "https://pds.example.org") == 0);
    CHECK(indigo_normalise_service("https://pds.example.org:8443//", out, sizeof out) == INDIGO_INPUT_OK);
    CHECK(strcmp(out, "https://pds.example.org:8443") == 0);

    /* Plain http is only for loopback, so credentials never go out in clear. */
    CHECK(indigo_normalise_service("http://pds.example.org", out, sizeof out) == INDIGO_INPUT_BAD_SCHEME);
    CHECK(indigo_normalise_service("http://localhost:2583", out, sizeof out) == INDIGO_INPUT_OK);
    CHECK(strcmp(out, "http://localhost:2583") == 0);
    CHECK(indigo_normalise_service("http://localhost.evil.example", out, sizeof out) == INDIGO_INPUT_BAD_SCHEME);
    CHECK(indigo_normalise_service("ftp://x.example", out, sizeof out) == INDIGO_INPUT_BAD_SCHEME);

    CHECK(indigo_normalise_service("", out, sizeof out) == INDIGO_INPUT_EMPTY);
    CHECK(indigo_normalise_service("   ", out, sizeof out) == INDIGO_INPUT_EMPTY);
    CHECK(indigo_normalise_service("https://", out, sizeof out) == INDIGO_INPUT_EMPTY);
    CHECK(indigo_normalise_service("a b.example", out, sizeof out) == INDIGO_INPUT_BAD_CHARS);
    CHECK(indigo_normalise_service("https://bsky.social", out, 8) == INDIGO_INPUT_TOO_LONG);
}

static void
test_normalise_handle(void)
{
    char out[INDIGO_HANDLE_MAX];

    CHECK(indigo_normalise_handle("@ewancroft.uk", out, sizeof out) == INDIGO_INPUT_OK);
    CHECK(strcmp(out, "ewancroft.uk") == 0);
    CHECK(indigo_normalise_handle(" me@ewancroft.uk \n", out, sizeof out) == INDIGO_INPUT_OK);
    CHECK(strcmp(out, "me@ewancroft.uk") == 0);
    CHECK(indigo_normalise_handle("@", out, sizeof out) == INDIGO_INPUT_EMPTY);
    CHECK(indigo_normalise_handle("two words", out, sizeof out) == INDIGO_INPUT_BAD_CHARS);
    CHECK(indigo_normalise_handle("did:plc:abc", out, 4) == INDIGO_INPUT_TOO_LONG);
}

static void
test_signin_fields(void)
{
    indigo_signin s;
    char shown[64];

    indigo_signin_init(&s);
    CHECK(strcmp(s.service, INDIGO_DEFAULT_SERVICE) == 0);
    CHECK(!indigo_signin_ready(&s));

    CHECK(indigo_signin_set_field(&s, INDIGO_FIELD_HANDLE, "@ewancroft.uk") == INDIGO_INPUT_OK);
    CHECK(indigo_signin_set_field(&s, INDIGO_FIELD_PASSWORD, "abcd-efgh-ijkl-mnop\n") == INDIGO_INPUT_OK);
    CHECK(strcmp(s.password, "abcd-efgh-ijkl-mnop") == 0);
    CHECK(indigo_signin_ready(&s));
    CHECK(indigo_signin_set_field(&s, INDIGO_FIELD_PASSWORD, "") == INDIGO_INPUT_EMPTY);
    CHECK(strcmp(s.password, "abcd-efgh-ijkl-mnop") == 0);

    /* The password is never shown, only its length. */
    indigo_signin_display(&s, INDIGO_FIELD_PASSWORD, shown, sizeof shown);
    CHECK(strlen(shown) == strlen("abcd-efgh-ijkl-mnop"));
    CHECK(strspn(shown, "*") == strlen(shown));
    indigo_signin_display(&s, INDIGO_FIELD_HANDLE, shown, sizeof shown);
    CHECK(strcmp(shown, "ewancroft.uk") == 0);
}

static void
test_signin_flow(void)
{
    indigo_app app;
    indigo_input in = {0};
    indigo_field f;
    indigo_rect r;

    indigo_app_init(&app);
    CHECK(app.screen == INDIGO_SCREEN_SIGNIN);

    /* Tapping a field asks the platform for text entry. */
    r = indigo_layout_button_rect(INDIGO_ACTION_FIELD_HANDLE);
    in.touch_pressed = true;
    in.touch_x = (int) (r.x + 4);
    in.touch_y = (int) (r.y + 4);
    indigo_app_update(&app, &in);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_EDIT_FIELD);
    CHECK(f == INDIGO_FIELD_HANDLE);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_NONE);

    CHECK(!indigo_app_set_field(&app, INDIGO_FIELD_SERVICE, "http://pds.example.org"));
    CHECK(app.signin.status_is_error);
    CHECK(strcmp(app.signin.service, INDIGO_DEFAULT_SERVICE) == 0);
    CHECK(indigo_app_set_field(&app, INDIGO_FIELD_SERVICE, "pds.example.org"));
    CHECK(!app.signin.status_is_error && app.signin.status[0] == '\0');
    CHECK(strcmp(app.signin.service, "https://pds.example.org") == 0);
    indigo_signin_set_field(&app.signin, INDIGO_FIELD_SERVICE, INDIGO_DEFAULT_SERVICE);

    /* Signing in with an empty form is refused with a message, not sent. */
    r = indigo_layout_button_rect(INDIGO_ACTION_SIGN_IN);
    in.touch_x = (int) (r.x + 4);
    in.touch_y = (int) (r.y + 4);
    indigo_app_update(&app, &in);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_NONE);
    CHECK(app.signin.status_is_error && app.signin.status[0]);

    indigo_signin_set_field(&app.signin, INDIGO_FIELD_HANDLE, "ewancroft.uk");
    indigo_signin_set_field(&app.signin, INDIGO_FIELD_PASSWORD, "aaaa-bbbb-cccc-dddd");
    indigo_app_update(&app, &in);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_SIGN_IN);

    /* While busy, further taps are ignored. */
    indigo_app_begin_sign_in(&app, "Signing in...");
    indigo_app_update(&app, &in);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_NONE);

    /* A failure returns to the form and keeps the handle but explains why. */
    indigo_app_sign_in_failed(&app, "Wrong handle or app password.");
    CHECK(app.screen == INDIGO_SCREEN_SIGNIN);
    CHECK(app.signin.phase == INDIGO_PHASE_IDLE);
    CHECK(app.signin.status_is_error);
    CHECK(strcmp(app.signin.handle, "ewancroft.uk") == 0);

    /* Success wipes the password from memory and moves to Home. */
    indigo_app_sign_in_succeeded(&app, "ewancroft.uk");
    CHECK(app.screen == INDIGO_SCREEN_HOME);
    CHECK(app.signin.password[0] == '\0');
    CHECK(strcmp(app.signin.account, "ewancroft.uk") == 0);

    /* Open the More menu the way the person does: with nothing selected it
     * holds only the app actions, and Sign out is the sixth (Find people and
     * Find posts sit between Notifications and My profile). */
    in = (indigo_input) {0};
    r = indigo_layout_button_rect(INDIGO_ACTION_MENU);
    in.touch_pressed = true;
    in.touch_x = (int) (r.x + 4);
    in.touch_y = (int) (r.y + 4);
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_MENU);
    CHECK(app.menu.count == 13);
    CHECK(app.menu.items[3].kind == INDIGO_MENU_FIND_POSTS);
    CHECK(app.menu.items[4].kind == INDIGO_MENU_LISTS);
    CHECK(app.menu.items[5].kind == INDIGO_MENU_FEEDS);
    CHECK(app.menu.items[6].kind == INDIGO_MENU_MUTED);
    CHECK(app.menu.items[7].kind == INDIGO_MENU_BLOCKED);
    CHECK(app.menu.items[9].kind == INDIGO_MENU_SETTINGS);
    CHECK(app.menu.items[10].kind == INDIGO_MENU_UPDATE);
    CHECK(app.menu.items[11].kind == INDIGO_MENU_SIGN_OUT);
    CHECK(strcmp(app.menu.items[8].label, "My profile") == 0);

    /* Sign out is the twelfth item, so it sits below the window until the
     * selection is moved onto it. */
    for (unsigned i = 0; i < 11; i++) {
        in = (indigo_input) {0};
        in.down = true;
        indigo_app_update(&app, &in);
    }
    in = (indigo_input) {0};
    r = indigo_layout_button_rect(INDIGO_ACTION_MENU4);
    in.touch_pressed = true;
    in.touch_x = (int) (r.x + 4);
    in.touch_y = (int) (r.y + 4);
    indigo_app_update(&app, &in);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_SIGN_OUT);
    indigo_app_signed_out(&app, "Signed out.");
    CHECK(app.screen == INDIGO_SCREEN_SIGNIN);
    CHECK(app.signin.account[0] == '\0');
}

static void
test_signin_targets_spaced(void)
{
    static const indigo_action a[] = {INDIGO_ACTION_FIELD_SERVICE, INDIGO_ACTION_FIELD_HANDLE,
                                      INDIGO_ACTION_FIELD_PASSWORD, INDIGO_ACTION_SIGN_IN};

    for (unsigned i = 0; i < 4; i++) {
        indigo_rect r = indigo_layout_button_rect(a[i]);

        CHECK(r.x >= 0 && r.x + r.w <= INDIGO_BOTTOM_WIDTH);
        CHECK(r.y + r.h <= INDIGO_BOTTOM_HEIGHT);
        CHECK(r.h >= 36.0f);
        if (i > 0) {
            indigo_rect p = indigo_layout_button_rect(a[i - 1]);

            CHECK(r.y - (p.y + p.h) >= 8.0f);
        }
    }
}

static void
fill_session(indigo_saved_session *s)
{
    memset(s, 0, sizeof *s);
    strcpy(s->service, "https://pds.example.org");
    strcpy(s->session, "{\"accessJwt\":\"aaa\",\"refreshJwt\":\"ddd\"}");
}

static void
test_session_codec(void)
{
    static indigo_saved_session in, out;
    static char buf[INDIGO_SESSION_FILE_MAX];
    size_t len = 0;

    fill_session(&in);
    CHECK(indigo_session_encode(&in, buf, sizeof buf, &len) == INDIGO_CODEC_OK);
    CHECK(indigo_session_decode(buf, len, &out) == INDIGO_CODEC_OK);
    CHECK(strcmp(out.service, in.service) == 0);
    CHECK(strcmp(out.session, in.session) == 0);

    /* Every truncation of a valid file is rejected, never half-loaded. */
    for (size_t cut = 0; cut < len; cut++) {
        indigo_codec_status st = indigo_session_decode(buf, cut, &out);

        CHECK(st != INDIGO_CODEC_OK);
        CHECK(out.session[0] == '\0');
    }

    CHECK(indigo_session_decode("", 0, &out) == INDIGO_CODEC_EMPTY);
    CHECK(indigo_session_decode("garbage\n", 8, &out) == INDIGO_CODEC_CORRUPT);
    CHECK(indigo_session_decode("indigo-session 9\nend\n", 21, &out) == INDIGO_CODEC_BAD_VERSION);
    {
        const char *noend = "indigo-session 1\nservice=x\nsession=z\n";
        const char *ok = "indigo-session 1\nfuture=1\nservice=x\nsession=z\nend\n";

        CHECK(indigo_session_decode(noend, strlen(noend), &out) == INDIGO_CODEC_CORRUPT);
        CHECK(indigo_session_decode(ok, strlen(ok), &out) == INDIGO_CODEC_OK);
    }

    /* Values that could forge extra lines are refused on the way in. */
    strcpy(in.session, "a\nrefresh=evil");
    CHECK(indigo_session_encode(&in, buf, sizeof buf, &len) == INDIGO_CODEC_CORRUPT);
    fill_session(&in);
    in.session[0] = '\0';
    CHECK(indigo_session_encode(&in, buf, sizeof buf, &len) == INDIGO_CODEC_INCOMPLETE);
    fill_session(&in);
    CHECK(indigo_session_encode(&in, buf, 20, &len) == INDIGO_CODEC_TOO_BIG);

    indigo_session_wipe(&out);
    CHECK(out.service[0] == '\0' && out.session[0] == '\0');
}

static void
test_session_store(void)
{
    static indigo_saved_session in, out;
    char dir[64], path[256], bad[300], cmd[300];

    snprintf(dir, sizeof dir, "build-host/store-test");
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s", dir, dir);
    CHECK(system(cmd) == 0);
    snprintf(path, sizeof path, "%s/session", dir);
    snprintf(bad, sizeof bad, "%s.bad", path);

    CHECK(indigo_session_store_load(path, &out) == INDIGO_STORE_MISSING);

    fill_session(&in);
    CHECK(indigo_session_store_save(path, &in) == INDIGO_STORE_OK);
    CHECK(indigo_session_store_save(path, &in) == INDIGO_STORE_OK); /* overwrite */
    CHECK(indigo_session_store_load(path, &out) == INDIGO_STORE_OK);
    CHECK(strcmp(out.session, in.session) == 0);

    CHECK(indigo_session_store_clear(path) == INDIGO_STORE_OK);
    CHECK(indigo_session_store_load(path, &out) == INDIGO_STORE_MISSING);
    CHECK(indigo_session_store_clear(path) == INDIGO_STORE_OK); /* idempotent */

    /* A corrupt file is recoverable and kept aside rather than destroyed. */
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f) {
        fputs("not a session file", f);
        fclose(f);
    }
    CHECK(indigo_session_store_load(path, &out) == INDIGO_STORE_UNREADABLE);
    CHECK(indigo_session_store_load(path, &out) == INDIGO_STORE_MISSING);
    f = fopen(bad, "rb");
    CHECK(f != NULL);
    if (f) {
        fclose(f);
    }

    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    CHECK(system(cmd) == 0);
}

static void
test_settings_defaults_and_clamp(void)
{
    indigo_settings s;

    indigo_settings_defaults(&s);
    CHECK(s.theme == INDIGO_THEME_AUTO);
    CHECK(s.text_scale == INDIGO_TEXT_SCALE_NORMAL);
    CHECK(!s.reduce_motion);
    CHECK(!s.high_contrast);
    CHECK(!s.large_targets);
    CHECK(s.diagnostics); /* on unless asked otherwise, as it was before */
    CHECK(s.default_feed[0] == '\0');

    /* Nothing out of range may survive clamp, whoever filled the struct. */
    s.theme = (indigo_theme) 99;
    s.text_scale = (indigo_text_scale) 101;
    memset(s.default_feed, 'a', sizeof s.default_feed);
    indigo_settings_clamp(&s);
    CHECK(s.theme == INDIGO_THEME_AUTO);
    CHECK(s.text_scale == INDIGO_TEXT_SCALE_NORMAL);
    CHECK(s.default_feed[INDIGO_SETTINGS_FEED_MAX - 1] == '\0');
}

static void
test_settings_codec(void)
{
    static indigo_settings in, out;
    static char buf[INDIGO_SETTINGS_FILE_MAX];
    size_t len = 0;

    indigo_settings_defaults(&in);
    /* Alt text is off by default: a reader looking at a photograph does not
     * also want its description. */
    CHECK(!in.alt_text);
    in.theme = INDIGO_THEME_DARK;
    in.text_scale = INDIGO_TEXT_SCALE_LARGE;
    in.reduce_motion = true;
    in.high_contrast = true;
    in.large_targets = true;
    in.alt_text = true;
    in.diagnostics = true;
    snprintf(in.default_feed, sizeof in.default_feed,
             "at://did:plc:abc123/app.bsky.feed.generator/daily");

    CHECK(indigo_settings_encode(&in, buf, sizeof buf, &len) == INDIGO_CODEC_OK);
    CHECK(indigo_settings_decode(buf, len, &out) == INDIGO_CODEC_OK);
    CHECK(out.theme == in.theme);
    CHECK(out.text_scale == in.text_scale);
    CHECK(out.reduce_motion == in.reduce_motion);
    CHECK(out.high_contrast == in.high_contrast);
    CHECK(out.large_targets == in.large_targets);
    CHECK(out.alt_text == in.alt_text);
    CHECK(out.diagnostics == in.diagnostics);
    CHECK(strcmp(out.default_feed, in.default_feed) == 0);

    /* Every truncation of a valid file is rejected, never half-loaded, and
     * still leaves out usable at its defaults. */
    for (size_t cut = 0; cut < len; cut++) {
        indigo_codec_status st = indigo_settings_decode(buf, cut, &out);

        CHECK(st != INDIGO_CODEC_OK);
        CHECK(out.default_feed[INDIGO_SETTINGS_FEED_MAX - 1] == '\0');
        CHECK(out.text_scale == INDIGO_TEXT_SCALE_NORMAL);
    }

    CHECK(indigo_settings_decode("", 0, &out) == INDIGO_CODEC_EMPTY);
    CHECK(indigo_settings_decode("garbage\n", 8, &out) == INDIGO_CODEC_CORRUPT);

    /* Unknown keys are skipped so a file from a newer Indigo still loads. */
    {
        const char *noend = "indigo-settings 1\ntheme=1\n";
        const char *ok = "indigo-settings 1\nfuture=1\ntheme=1\nend\n";

        CHECK(indigo_settings_decode(noend, strlen(noend), &out) ==
              INDIGO_CODEC_CORRUPT);
        CHECK(indigo_settings_decode(ok, strlen(ok), &out) == INDIGO_CODEC_OK);
        CHECK(out.theme == INDIGO_THEME_LIGHT);
    }

    /* Keys the file omits take their defaults rather than failing the file. */
    {
        const char *partial = "indigo-settings 1\nlarge_targets=1\nalt_text=1\nend\n";

        CHECK(indigo_settings_decode(partial, strlen(partial), &out) ==
              INDIGO_CODEC_OK);
        CHECK(out.large_targets);
        CHECK(out.alt_text);
        CHECK(out.theme == INDIGO_THEME_AUTO);
        CHECK(out.text_scale == INDIGO_TEXT_SCALE_NORMAL);
        CHECK(out.default_feed[0] == '\0');
    }

    /* One unusable value must not cost the user the rest of the file. */
    {
        const char *mixed = "indigo-settings 1\ntheme=9\ntext_scale=101\n"
                            "high_contrast=yes\nlarge_targets=1\nalt_text=2\nend\n";

        CHECK(indigo_settings_decode(mixed, strlen(mixed), &out) ==
              INDIGO_CODEC_OK);
        CHECK(out.theme == INDIGO_THEME_AUTO);              /* out of range */
        CHECK(out.text_scale == INDIGO_TEXT_SCALE_NORMAL);  /* in range, but
                                                             * not a scale */
        CHECK(!out.high_contrast);                          /* not a boolean */
        CHECK(!out.alt_text);                               /* not a boolean */
        CHECK(out.large_targets);                           /* the good one */
    }

    /* Signed, spaced and oversized numbers are all refused the same way. */
    {
        const char *junk = "indigo-settings 1\ntheme=+1\ntext_scale=9999\n"
                           "reduce_motion=2\nend\n";

        CHECK(indigo_settings_decode(junk, strlen(junk), &out) == INDIGO_CODEC_OK);
        CHECK(out.theme == INDIGO_THEME_AUTO);
        CHECK(out.text_scale == INDIGO_TEXT_SCALE_NORMAL);
        CHECK(!out.reduce_motion);
    }

    /* A file last edited on a CRLF machine still loads. */
    {
        const char *crlf = "indigo-settings 1\r\ntheme=2\r\nend\r\n";

        CHECK(indigo_settings_decode(crlf, strlen(crlf), &out) == INDIGO_CODEC_OK);
        CHECK(out.theme == INDIGO_THEME_DARK);
    }

    /* A feed longer than the field can hold is dropped, not truncated into
     * something that would resolve to the wrong feed. */
    {
        char feed[201];
        char big[INDIGO_SETTINGS_FILE_MAX];
        char *tail;
        int w;

        memset(feed, 'x', 200);
        feed[200] = '\0';
        tail = strcpy(big, "indigo-settings 1\ndefault_feed=");
        tail += strlen(tail);
        w = snprintf(tail, sizeof big - (size_t) (tail - big), "%s\nend\n", feed);
        CHECK(w > 0);
        CHECK(indigo_settings_decode(big, (size_t) (tail - big) + (size_t) w,
                                     &out) == INDIGO_CODEC_OK);
        CHECK(out.default_feed[0] == '\0');
    }

    /* Values that could forge extra lines are refused on the way in. */
    snprintf(in.default_feed, sizeof in.default_feed, "at://x\ntheme=2");
    CHECK(indigo_settings_encode(&in, buf, sizeof buf, &len) ==
          INDIGO_CODEC_CORRUPT);

    indigo_settings_defaults(&in);
    CHECK(indigo_settings_encode(&in, buf, 20, &len) == INDIGO_CODEC_TOO_BIG);

    /* encode clamps, so a caller that skipped clamp cannot write a file this
     * decoder would have to reject. */
    in.theme = (indigo_theme) 42;
    in.text_scale = (indigo_text_scale) 7;
    CHECK(indigo_settings_encode(&in, buf, sizeof buf, &len) == INDIGO_CODEC_OK);
    CHECK(indigo_settings_decode(buf, len, &out) == INDIGO_CODEC_OK);
    CHECK(out.theme == INDIGO_THEME_AUTO);
    CHECK(out.text_scale == INDIGO_TEXT_SCALE_NORMAL);
}

static void
test_draft_store(void)
{
    char buf[2048];
    char out[1024];
    char dir[64], path[256], bad[300], cmd[300];
    size_t len = 0;
    const char *text = "Line one\nline two with \xc3\xa9 and \xf0\x9f\x98\x80";

    /* Round trip, including newlines and multibyte text. */
    CHECK(indigo_draft_encode(text, buf, sizeof buf, &len) == INDIGO_CODEC_OK);
    CHECK(indigo_draft_decode(buf, len, out, sizeof out) == INDIGO_CODEC_OK);
    CHECK(strcmp(out, text) == 0);

    /* Every strict prefix is refused and leaves out alone. */
    for (size_t n = 0; n < len; n++) {
        strcpy(out, "sentinel");
        CHECK(indigo_draft_decode(buf, n, out, sizeof out) != INDIGO_CODEC_OK);
        CHECK(strcmp(out, "sentinel") == 0);
    }
    CHECK(indigo_draft_decode("indigo-draft 2\nlen=1\nx\nend\n", 27, out, sizeof out) ==
          INDIGO_CODEC_BAD_VERSION);
    CHECK(indigo_draft_decode("garbage that is not a draft", 27, out, sizeof out) ==
          INDIGO_CODEC_CORRUPT);
    /* A declared length larger than the buffer is refused, not trusted. */
    CHECK(indigo_draft_decode("indigo-draft 1\nlen=999999\n", 27, out, 16) ==
          INDIGO_CODEC_TOO_BIG);
    CHECK(indigo_draft_encode("0123456789", buf, 20, &len) == INDIGO_CODEC_TOO_BIG);

    snprintf(dir, sizeof dir, "build-host/draft-test");
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s", dir, dir);
    CHECK(system(cmd) == 0);
    snprintf(path, sizeof path, "%s/draft.dat", dir);
    snprintf(bad, sizeof bad, "%s.bad", path);

    CHECK(indigo_draft_store_load(path, out, sizeof out) == INDIGO_STORE_MISSING);
    CHECK(out[0] == '\0');
    CHECK(indigo_draft_store_save(path, text) == INDIGO_STORE_OK);
    CHECK(indigo_draft_store_load(path, out, sizeof out) == INDIGO_STORE_OK);
    CHECK(strcmp(out, text) == 0);

    /* An empty draft removes the file, and removing nothing is not an error. */
    CHECK(indigo_draft_store_save(path, "") == INDIGO_STORE_OK);
    CHECK(indigo_draft_store_load(path, out, sizeof out) == INDIGO_STORE_MISSING);
    CHECK(indigo_draft_store_save(path, "") == INDIGO_STORE_OK);

    /* A damaged file is moved aside, not deleted, and loads as empty. */
    {
        FILE *f = fopen(path, "wb");

        CHECK(f != NULL);
        if (f) {
            fputs("indigo-draft 1\nlen=50\nshort", f);
            fclose(f);
        }
    }
    CHECK(indigo_draft_store_load(path, out, sizeof out) == INDIGO_STORE_UNREADABLE);
    CHECK(out[0] == '\0');
    {
        FILE *f = fopen(bad, "rb");

        CHECK(f != NULL);
        if (f) {
            fclose(f);
        }
    }
    CHECK(indigo_draft_store_load(path, out, sizeof out) == INDIGO_STORE_MISSING);
}

static void
test_settings_store(void)
{
    static indigo_settings in, out;
    char dir[64], path[256], bad[300], cmd[300];

    snprintf(dir, sizeof dir, "build-host/settings-test");
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s", dir, dir);
    CHECK(system(cmd) == 0);
    snprintf(path, sizeof path, "%s/settings.dat", dir);
    snprintf(bad, sizeof bad, "%s.bad", path);

    /* A first run has no file, and that is not an error: the caller gets the
     * defaults and carries on. */
    CHECK(indigo_settings_store_load(path, &out) == INDIGO_STORE_MISSING);
    CHECK(out.theme == INDIGO_THEME_AUTO);
    CHECK(out.default_feed[0] == '\0');

    indigo_settings_defaults(&in);
    in.theme = INDIGO_THEME_DARK;
    in.high_contrast = true;
    snprintf(in.default_feed, sizeof in.default_feed, "at://did:plc:abc/x");

    CHECK(indigo_settings_store_save(path, &in) == INDIGO_STORE_OK);
    CHECK(indigo_settings_store_save(path, &in) == INDIGO_STORE_OK); /* overwrite */
    CHECK(indigo_settings_store_load(path, &out) == INDIGO_STORE_OK);
    CHECK(out.theme == INDIGO_THEME_DARK);
    CHECK(out.high_contrast);
    CHECK(strcmp(out.default_feed, in.default_feed) == 0);

    CHECK(indigo_settings_store_clear(path) == INDIGO_STORE_OK);
    CHECK(indigo_settings_store_load(path, &out) == INDIGO_STORE_MISSING);
    CHECK(indigo_settings_store_clear(path) == INDIGO_STORE_OK); /* idempotent */

    /* A damaged file is kept aside rather than destroyed, and the app still
     * gets usable defaults instead of failing to boot. */
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f) {
        fputs("not a settings file", f);
        fclose(f);
    }
    CHECK(indigo_settings_store_load(path, &out) == INDIGO_STORE_UNREADABLE);
    CHECK(out.theme == INDIGO_THEME_AUTO);
    CHECK(out.text_scale == INDIGO_TEXT_SCALE_NORMAL);
    CHECK(indigo_settings_store_load(path, &out) == INDIGO_STORE_MISSING);
    f = fopen(bad, "rb");
    CHECK(f != NULL);
    if (f) {
        fclose(f);
    }

    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    CHECK(system(cmd) == 0);
}

static void
test_settings_reach_the_app(void)
{
    static indigo_app app;
    indigo_settings s;
    indigo_field f;

    /* init() zeroes the struct, and a zeroed text_scale is not a scale. */
    indigo_app_init(&app);
    CHECK(app.settings.text_scale == INDIGO_TEXT_SCALE_NORMAL);
    CHECK(app.settings.diagnostics);

    indigo_settings_defaults(&s);
    s.text_scale = INDIGO_TEXT_SCALE_LARGE;
    s.high_contrast = true;
    s.default_feed[0] = '\0';
    indigo_app_set_settings(&app, &s);
    CHECK(app.settings.text_scale == INDIGO_TEXT_SCALE_LARGE);
    CHECK(app.settings.high_contrast);

    /* An out-of-range value from a caller is clamped on the way in, the same
     * as one read off disk. */
    s.text_scale = (indigo_text_scale) 101;
    indigo_app_set_settings(&app, &s);
    CHECK(app.settings.text_scale == INDIGO_TEXT_SCALE_NORMAL);

    /* No default feed means the plain Following timeline, as before. */
    indigo_app_sign_in_succeeded(&app, "me.example");
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_TIMELINE_REFRESH);
    CHECK(app.feed_uri[0] == '\0');

    /* A default feed is what Home opens on. It must not push a history entry,
     * or B from Home would go back to sign-in. */
    indigo_app_init(&app);
    indigo_settings_defaults(&s);
    snprintf(s.default_feed, sizeof s.default_feed,
             "at://did:plc:abc/app.bsky.feed.generator/daily");
    indigo_app_set_settings(&app, &s);
    indigo_app_sign_in_succeeded(&app, "me.example");
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_FEED);
    CHECK(strcmp(app.feed_uri, s.default_feed) == 0);
    CHECK(strcmp(app.request_feed_uri, s.default_feed) == 0);
    CHECK(app.feed_name[0] != '\0');
    CHECK(app.history_count == 0);
}

static void
test_failures(void)
{

    for (int f = WF_FAIL_BAD_CREDENTIALS; f <= WF_FAIL_OTHER; f++) {
        const char *m = indigo_failure_message((wf_failure_kind) f);

        CHECK(m[0] != '\0');
        CHECK(strlen(m) < INDIGO_STATUS_MAX);
        CHECK(wf_failure_tag((wf_failure_kind) f)[0] != '\0');
    }
}

static void
test_log_file(void)
{
    char buf[128];
    FILE *f;

    mkdir("build-host", 0777);
    remove("build-host/test.log.old");
    CHECK(indigo_log_open_file("build-host/test.log"));
    indigo_log_info("first run %d", 1);
    indigo_log_shutdown();

    CHECK(indigo_log_open_file("build-host/test.log"));
    indigo_log_error("second run");
    indigo_log_shutdown();

    f = fopen("build-host/test.log.old", "r");
    CHECK(f != NULL);
    if (f) {
        CHECK(fgets(buf, sizeof buf, f) != NULL && strstr(buf, "first run 1") != NULL);
        fclose(f);
    }
    f = fopen("build-host/test.log", "r");
    CHECK(f != NULL);
    if (f) {
        CHECK(fgets(buf, sizeof buf, f) != NULL && strstr(buf, "second run") != NULL);
        fclose(f);
    }
    CHECK(!indigo_log_open_file("/nonexistent-dir/x.log"));
}

static void
test_autofill(void)
{
    indigo_signin s;

    indigo_signin_init(&s);
    CHECK(indigo_signin_apply_autofill(&s, "service=pds.example.org\nhandle=@me.example\npassword=abcd-efgh\n") == 3);
    CHECK(strcmp(s.service, "https://pds.example.org") == 0);
    CHECK(strcmp(s.handle, "me.example") == 0);
    CHECK(strcmp(s.password, "abcd-efgh") == 0);
    CHECK(indigo_signin_ready(&s));

    indigo_signin_init(&s);
    CHECK(indigo_signin_apply_autofill(&s, "junk\nhandle=\npassword=x") == 1);
    CHECK(strcmp(s.service, INDIGO_DEFAULT_SERVICE) == 0);
    CHECK(!indigo_signin_ready(&s));
    CHECK(indigo_signin_apply_autofill(&s, "") == 0);
}

static void
fake_post(indigo_post *p, unsigned i)
{
    memset(p, 0, sizeof *p);
    snprintf(p->uri, sizeof p->uri, "at://did:plc:x/app.bsky.feed.post/%u", i);
    snprintf(p->text, sizeof p->text, "post %u", i);
}

static void
test_timeline_bounds(void)
{
    indigo_timeline *t = calloc(1, sizeof *t);
    indigo_post p;

    indigo_timeline_init(t);
    for (unsigned i = 0; i < INDIGO_TIMELINE_MAX; i++) {
        fake_post(&p, i);
        CHECK(indigo_timeline_append(t, &p));
    }
    fake_post(&p, 999);
    CHECK(!indigo_timeline_append(t, &p));
    CHECK(t->count == INDIGO_TIMELINE_MAX);
    free(t);
}

static void
test_timeline_selection(void)
{
    indigo_timeline *t = calloc(1, sizeof *t);
    indigo_post p;

    indigo_timeline_init(t);
    CHECK(indigo_timeline_selected(t) == NULL);
    CHECK(!indigo_timeline_move(t, 1, 3));
    for (unsigned i = 0; i < 10; i++) {
        fake_post(&p, i);
        indigo_timeline_append(t, &p);
    }
    CHECK(!indigo_timeline_move(t, -1, 3));
    CHECK(indigo_timeline_move(t, 1, 3));
    CHECK(indigo_timeline_move(t, 1, 3));
    CHECK(t->scroll == 0);
    CHECK(indigo_timeline_move(t, 1, 3));
    CHECK(t->selected == 3 && t->scroll == 1);
    CHECK(indigo_timeline_move(t, 100, 3));
    CHECK(t->selected == 9 && t->scroll == 7);
    CHECK(!indigo_timeline_move(t, 1, 3));
    CHECK(indigo_timeline_move(t, -100, 3));
    CHECK(t->selected == 0 && t->scroll == 0);
    CHECK(strcmp(indigo_timeline_selected(t)->text, "post 0") == 0);
    free(t);
}

static void
test_timeline_paging(void)
{
    indigo_timeline *t = calloc(1, sizeof *t);
    indigo_post p;

    indigo_timeline_init(t);
    indigo_timeline_begin_fetch(t, true);
    CHECK(t->loading);
    for (unsigned i = 0; i < 10; i++) {
        fake_post(&p, i);
        indigo_timeline_append(t, &p);
    }
    indigo_timeline_finish_fetch(t, "cursor-1");
    CHECK(t->has_more && !t->loading);
    CHECK(!indigo_timeline_wants_page(t));
    indigo_timeline_select(t, 5, 3);
    CHECK(indigo_timeline_wants_page(t));
    indigo_timeline_begin_fetch(t, false);
    CHECK(!indigo_timeline_wants_page(t));
    CHECK(t->count == 10);
    indigo_timeline_fail_fetch(t, "Network error.");
    CHECK(t->status_is_error && !t->loading);
    CHECK(indigo_timeline_wants_page(t));
    indigo_timeline_begin_fetch(t, false);
    indigo_timeline_finish_fetch(t, "");
    CHECK(!t->has_more && !indigo_timeline_wants_page(t));
    indigo_timeline_begin_fetch(t, true);
    CHECK(t->count == 0 && t->cursor[0] == '\0');
    free(t);
}

static void
test_timeline_actions(void)
{
    indigo_timeline *t = calloc(1, sizeof *t);
    indigo_post p;

    indigo_timeline_init(t);
    fake_post(&p, 1);
    p.like_count = 4;
    indigo_timeline_append(t, &p);
    CHECK(indigo_timeline_set_like(t, p.uri, NULL, true));
    CHECK(t->posts[0].like_pending && t->posts[0].like_count == 4);
    CHECK(indigo_timeline_set_like(t, p.uri, "at://like/1", false));
    CHECK(!t->posts[0].like_pending && t->posts[0].like_count == 5);
    CHECK(indigo_timeline_set_like(t, p.uri, "", false));
    CHECK(t->posts[0].like_count == 4 && t->posts[0].like_uri[0] == '\0');
    CHECK(indigo_timeline_set_repost(t, p.uri, "at://rp/1", false));
    CHECK(t->posts[0].repost_count == 1);
    CHECK(!indigo_timeline_set_like(t, "at://gone", "x", false));
    free(t);
}

static void
test_copy_utf8(void)
{
    char b[5];

    indigo_copy_utf8(b, sizeof b, "abcdefgh");
    CHECK(strcmp(b, "abcd") == 0);
    /* "é" is two bytes; a cut through the middle must drop it whole. */
    indigo_copy_utf8(b, sizeof b, "abc\xC3\xA9");
    CHECK(strcmp(b, "abc") == 0);
    indigo_copy_utf8(b, sizeof b, NULL);
    CHECK(b[0] == '\0');
}

static void
test_wrap(void)
{
    indigo_line l[6];
    int cut;
    unsigned n;

    n = indigo_wrap("hello brave new world", 11, l, 6, &cut);
    CHECK(n == 2 && !cut);
    CHECK(l[0].start == 0 && l[0].len == 11);
    CHECK(l[1].start == 12 && l[1].len == 9);

    n = indigo_wrap("a\n\nb", 10, l, 6, &cut);
    CHECK(n == 3 && l[1].len == 0 && l[2].start == 3);

    n = indigo_wrap("abcdefghij", 4, l, 6, &cut);
    CHECK(n == 3 && l[0].len == 4 && l[2].len == 2);

    n = indigo_wrap("one two three four five six", 8, l, 2, &cut);
    CHECK(n == 2 && cut);

    /* Never split inside a multi-byte character. */
    n = indigo_wrap("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 3, l, 6, &cut);
    CHECK(n == 2 && l[0].len == 6 && l[1].len == 2);

    CHECK(indigo_wrap("", 10, l, 6, &cut) == 0);
    CHECK(indigo_wrap("x", 0, l, 6, &cut) == 0);
}

static void
test_canvas_spans(void)
{
    indigo_canvas c;
    indigo_segment seg[2 * INDIGO_CANVAS_MAX_SPANS + 1];
    unsigned n;

    indigo_canvas_init(&c, 100, 50);
    CHECK(!indigo_canvas_span(&c, 0, 1, 7));
    indigo_canvas_text(&c, 0, 0, 1.0f, 1, "see example.com now");
    CHECK(indigo_canvas_span(&c, 4, 15, 2));
    n = indigo_canvas_segments(&c, &c.cmds[0], seg);
    CHECK(n == 3);
    CHECK(seg[0].start == 0 && seg[0].end == 4 && seg[0].color == 1);
    CHECK(seg[1].start == 4 && seg[1].end == 15 && seg[1].color == 2);
    CHECK(seg[2].start == 15 && seg[2].end == 19 && seg[2].color == 1);

    indigo_canvas_text(&c, 0, 0, 1.0f, 1, "plain");
    n = indigo_canvas_segments(&c, &c.cmds[1], seg);
    CHECK(n == 1 && seg[0].end == 5);

    /* A span at the very start and a second one are both honoured. */
    indigo_canvas_text(&c, 0, 0, 1.0f, 1, "@a and #b");
    CHECK(indigo_canvas_span(&c, 0, 2, 5));
    CHECK(indigo_canvas_span(&c, 7, 9, 6));
    n = indigo_canvas_segments(&c, &c.cmds[2], seg);
    CHECK(n == 3 && seg[2].color == 6 && seg[2].end == 9);
}

static void
test_home_requests(void)
{
    indigo_app *app = calloc(1, sizeof *app);
    indigo_input in = {0};
    indigo_post p;
    indigo_field f;

    indigo_app_init(app);
    indigo_app_sign_in_succeeded(app, "me.example");
    CHECK(indigo_app_take_request(app, &f) == INDIGO_REQUEST_TIMELINE_REFRESH);
    CHECK(app->timeline.loading);

    for (unsigned i = 0; i < 8; i++) {
        fake_post(&p, i);
        snprintf(p.cid, sizeof p.cid, "cid%u", i);
        indigo_timeline_append(&app->timeline, &p);
    }
    indigo_timeline_finish_fetch(&app->timeline, "next");

    /* Like, then a second press while pending does nothing. */
    in.like = true;
    indigo_app_update(app, &in);
    CHECK(app->request == INDIGO_REQUEST_LIKE);
    CHECK(strcmp(app->request_post_uri, app->timeline.posts[0].uri) == 0);
    CHECK(strcmp(app->request_post_cid, "cid0") == 0);
    CHECK(app->timeline.posts[0].like_pending);
    CHECK(indigo_app_take_request(app, &f) == INDIGO_REQUEST_LIKE);
    indigo_app_update(app, &in);
    CHECK(app->request == INDIGO_REQUEST_NONE);

    /* Once liked, the same button undoes it using the record URI. */
    indigo_timeline_set_like(&app->timeline, app->timeline.posts[0].uri, "at://like/1", false);
    indigo_app_update(app, &in);
    CHECK(app->request == INDIGO_REQUEST_UNLIKE);
    CHECK(strcmp(app->request_undo_uri, "at://like/1") == 0);
    indigo_app_take_request(app, &f);
    indigo_timeline_set_like(&app->timeline, app->timeline.posts[0].uri, "", false);

    in = (indigo_input) {0};
    in.repost = true;
    indigo_app_update(app, &in);
    CHECK(app->request == INDIGO_REQUEST_REPOST);
    indigo_app_take_request(app, &f);

    /* Moving near the end asks for the next page exactly once. */
    in = (indigo_input) {0};
    in.page_down = true;
    indigo_app_update(app, &in);
    indigo_app_update(app, &in);
    CHECK(app->timeline.selected == 6);
    CHECK(app->request == INDIGO_REQUEST_TIMELINE_MORE);
    CHECK(app->timeline.loading);
    indigo_app_take_request(app, &f);
    in = (indigo_input) {0};
    indigo_app_update(app, &in);
    CHECK(app->request == INDIGO_REQUEST_NONE);

    /* Tapping a row selects it. */
    indigo_timeline_finish_fetch(&app->timeline, "");
    in.touch_pressed = true;
    in.touch_x = 100;
    in.touch_y = (int) (indigo_layout_button_rect(INDIGO_ACTION_ROW1).y + 5);
    indigo_app_update(app, &in);
    CHECK(app->timeline.selected == app->timeline.scroll + 1);

    /* Signing out forgets the timeline. */
    indigo_app_signed_out(app, "Signed out.");
    CHECK(app->timeline.count == 0);
    free(app);
}

static void
test_facet_menu(void)
{
    indigo_app app;
    indigo_input in = {0};
    indigo_field f;
    indigo_menu menu;
    indigo_post p = make_post("at://a/app.bsky.feed.post/1", "hi");
    const char *text = "hi @alice.example.com and #cats see https://example.com/x";

    /* Facets come from the post with their targets; the labels are the text
     * the person can actually see in the post. */
    snprintf(p.text, sizeof p.text, "%s", text);
    p.facet_count = 3;
    p.facets[0] = (indigo_post_facet) {INDIGO_FACET_MENTION, 3, 21,
                                        "did:plc:alice0000000000000000000000"};
    p.facets[1] = (indigo_post_facet) {INDIGO_FACET_TAG, 26, 31, "cats"};
    p.facets[2] = (indigo_post_facet) {INDIGO_FACET_LINK, 36, 58, "https://example.com/x"};

    indigo_menu_build(&menu, &p, "me.example.com");
    /* Three facet targets, the two entries about the post, then the twelve app
     * actions. */
    CHECK(menu.count == 18);
    CHECK(menu.items[0].kind == INDIGO_MENU_OPEN_MENTION);
    CHECK(strcmp(menu.items[0].label, "Profile: @alice.example.com") == 0);
    CHECK(strcmp(menu.items[0].payload, "did:plc:alice0000000000000000000000") == 0);
    CHECK(menu.items[1].kind == INDIGO_MENU_SHOW_TAG);
    CHECK(strcmp(menu.items[1].label, "Tag: #cats") == 0);
    CHECK(menu.items[2].kind == INDIGO_MENU_SHOW_LINK);
    CHECK(strcmp(menu.items[2].label, "Link: https://example.com/x") == 0);
    CHECK(menu.items[3].kind == INDIGO_MENU_LIKED_BY);
    CHECK(menu.items[4].kind == INDIGO_MENU_REPOSTED_BY);
    CHECK(strcmp(menu.post_uri, p.uri) == 0);
    CHECK(menu.items[5].kind == INDIGO_MENU_COMPOSE);
    CHECK(menu.items[17].kind == INDIGO_MENU_CLOSE);

    /* Choosing a mention opens that person's profile by did. */
    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_HOME;
    indigo_app_sign_in_succeeded(&app, "me.example.com");
    indigo_timeline_append(&app.timeline, &p);
    /* Signing in asks for the first page; take it so the menu is free. */
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_TIMELINE_REFRESH);
    {
        indigo_rect r = indigo_layout_button_rect(INDIGO_ACTION_MENU);

        in.touch_pressed = true;
        in.touch_x = (int) (r.x + 4);
        in.touch_y = (int) (r.y + 4);
    }
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_MENU);
    CHECK(app.menu.count == 18);

    in = (indigo_input) {0};
    in.confirm = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_PROFILE);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_PROFILE);
    CHECK(strcmp(app.request_post_uri, "did:plc:alice0000000000000000000000") == 0);
}

/* "Who liked this" and "Who reposted this": the menu opens a people list
 * keyed by the post's URI, which is longer than a handle and must arrive whole. */
static void
test_liked_by_from_the_menu(void)
{
    indigo_app app;
    indigo_input in = {0};
    indigo_post p = make_post("at://did:plc:alice0000000000000000000000/app.bsky.feed.post/3kabc", "hi");
    indigo_request_kind k;
    indigo_field f;

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_HOME;
    indigo_app_sign_in_succeeded(&app, "me.example.com");
    indigo_timeline_append(&app.timeline, &p);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_TIMELINE_REFRESH);
    {
        indigo_rect r = indigo_layout_button_rect(INDIGO_ACTION_MENU);

        in.touch_pressed = true;
        in.touch_x = (int) (r.x + 4);
        in.touch_y = (int) (r.y + 4);
    }
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_MENU);
    CHECK(app.menu.items[0].kind == INDIGO_MENU_LIKED_BY);

    in = (indigo_input) {0};
    in.confirm = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_SEARCH);
    CHECK(app.search.kind == INDIGO_SEARCH_LIKED_BY);
    CHECK(strcmp(indigo_search_title(&app.search), "Liked by") == 0);
    k = indigo_app_take_request(&app, &f);
    CHECK(k == INDIGO_REQUEST_PEOPLE);
    CHECK(app.request_people == INDIGO_SEARCH_LIKED_BY);
    CHECK(strcmp(app.request_subject, p.uri) == 0); /* whole, not truncated */

    /* B returns to the timeline the post is on, not past it. */
    in = (indigo_input) {0};
    in.back = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_HOME);
}

static void
test_facet_menu_edges(void)
{
    indigo_menu menu;
    indigo_post p = make_post("at://a/app.bsky.feed.post/1", "hi");

    /* A facet with no target is not offered: nothing could be opened. */
    snprintf(p.text, sizeof p.text, "%s", "hello @nobody.example.com");
    p.facet_count = 1;
    p.facets[0] = (indigo_post_facet) {INDIGO_FACET_MENTION, 6, 27, ""};
    indigo_menu_build(&menu, &p, "me.example.com");
    CHECK(menu.count == 15);
    CHECK(menu.items[0].kind == INDIGO_MENU_LIKED_BY);
    CHECK(menu.items[2].kind == INDIGO_MENU_COMPOSE);

    /* Byte ranges past the end of the text are ignored, not read out of
     * bounds. */
    p.facets[0] = (indigo_post_facet) {INDIGO_FACET_LINK, 400, 900, "https://example.com"};
    p.text[sizeof p.text - 1] = '\0';
    indigo_menu_build(&menu, &p, "me.example.com");
    CHECK(menu.count == 15);

    /* An empty account does not claim to know whose profile it is. */
    indigo_menu_build(&menu, NULL, "");
    CHECK(menu.count == 13);
    CHECK(strcmp(menu.items[8].label, "Your profile") == 0);
    CHECK(strcmp(menu.items[4].payload, "") == 0);

    /* More items than rows: the selection scrolls and stays in the window. */
    {
        indigo_post big = make_post("at://a/app.bsky.feed.post/2", "x");
        char text[INDIGO_POST_TEXT_MAX];
        size_t n = 0;

        big.text[0] = '\0';
        big.facet_count = 0;
        for (unsigned i = 0; i < INDIGO_POST_FACETS_MAX; i++) {
            int w = snprintf(text + n, sizeof text - n, "#tag%d ", i);

            if (w < 0 || (size_t) w >= sizeof text - n) {
                break;
            }
            n += (size_t) w;
            big.facets[big.facet_count] = (indigo_post_facet) {
                INDIGO_FACET_TAG, (unsigned) n - 5, (unsigned) n - 1, "tag"};
            big.facet_count++;
        }
        indigo_copy_utf8(big.text, sizeof big.text, text);

        indigo_menu_build(&menu, &big, "me.example.com");
        /* Eight facets plus the twelve app actions, and no more than the cap. */
        CHECK(menu.count == INDIGO_POST_FACETS_MAX + 15);
        CHECK(menu.count <= INDIGO_MENU_MAX);
        CHECK(menu.scroll == 0);

        /* Rows outside the count are empty rather than stale. */
        CHECK(indigo_menu_row(&menu, INDIGO_MENU_ROWS - 1, INDIGO_MENU_ROWS) != NULL);

        for (unsigned i = 0; i < INDIGO_MENU_ROWS; i++) {
            CHECK(indigo_menu_move(&menu, 1, INDIGO_MENU_ROWS));
        }
        CHECK(menu.selected == INDIGO_MENU_ROWS);
        CHECK(menu.scroll == 1);
        CHECK(indigo_menu_row(&menu, 0, INDIGO_MENU_ROWS) == &menu.items[1]);

        /* The selection stops at the last item instead of running off. */
        for (unsigned i = 0; i < INDIGO_MENU_MAX; i++) {
            indigo_menu_move(&menu, 1, INDIGO_MENU_ROWS);
        }
        CHECK(menu.selected == menu.count - 1);
        CHECK(menu.scroll + INDIGO_MENU_ROWS <= menu.count);
        CHECK(!indigo_menu_move(&menu, 1, INDIGO_MENU_ROWS));
    }
}

static void
test_menu_rows_on_screen(void)
{
    indigo_menu menu;

    indigo_menu_build(&menu, NULL, "me.example.com");

    /* Visible rows stay on the bottom screen, spaced apart, and the whole
     * menu fits under its header. */
    for (unsigned i = 0; i < INDIGO_MENU_ROWS; i++) {
        indigo_rect r = indigo_layout_button_rect((indigo_action) (INDIGO_ACTION_MENU0 + i));

        CHECK(r.x >= 0 && r.x + r.w <= INDIGO_BOTTOM_WIDTH);
        CHECK(r.y + r.h <= INDIGO_BOTTOM_HEIGHT);
        CHECK(r.h >= 34.0f);
        if (i > 0) {
            indigo_rect p = indigo_layout_button_rect((indigo_action) (INDIGO_ACTION_MENU0 + i - 1));

            CHECK(r.y - (p.y + p.h) >= 4.0f);
        }
    }
    /* INDIGO_ACTION_MENU4 is the last row the layout knows about. */
    CHECK(INDIGO_ACTION_MENU0 + INDIGO_MENU_ROWS - 1 == INDIGO_ACTION_MENU4);
}

/* Which button glyphs a text command mentions. Hints are written either as
 * "B  Back" in a title bar or "B Back" on a pill, so split on spaces. */
static void
collect_glyphs(const indigo_canvas *c, bool *seen)
{
    static const char *const glyphs[] = {"A", "B", "X", "Y", "R", "SEL", "START"};

    for (unsigned i = 0; i < c->count; i++) {
        const indigo_cmd *cmd = &c->cmds[i];
        const char *p;
        char word[16];

        if (cmd->kind != INDIGO_CMD_TEXT) {
            continue;
        }
        p = indigo_canvas_cmd_text(c, cmd);
        for (const char *at = p;; at++) {
            unsigned n = 0;

            while (*at && *at != ' ' && n + 1 < sizeof word) {
                word[n++] = *at++;
            }
            word[n] = '\0';
            if (*at && *at == ' ') {
                at++;
            }
            for (unsigned g = 0; g < sizeof glyphs / sizeof glyphs[0]; g++) {
                if (n && strcmp(word, glyphs[g]) == 0) {
                    seen[g] = true;
                }
            }
            if (!*at) {
                break;
            }
        }
    }
}

/* A control is hinted once: the bottom pills name the list actions, and the
 * title bar only what they cannot. Repeating a glyph would be a second hint
 * for the same control. */
static void
check_hints_once(const indigo_app *app, const indigo_input *in, unsigned screen)
{
    indigo_canvas top;
    indigo_canvas bottom;
    bool on_top[7] = {false};
    bool on_bottom[7] = {false};

    indigo_layout_build(app, in, &top, &bottom);
    if (top.overflow || bottom.overflow) {
        s_checks++;
        s_failures++;
        fprintf(stderr, "%s:%d: display list full on screen %u\n", __FILE__, __LINE__,
                screen);
        return;
    }
    collect_glyphs(&top, on_top);
    collect_glyphs(&bottom, on_bottom);
    for (unsigned g = 0; g < sizeof on_top / sizeof on_top[0]; g++) {
        if (on_top[g] && on_bottom[g]) {
            s_checks++;
            s_failures++;
            fprintf(stderr, "%s:%d: screen %u hints one control twice\n", __FILE__,
                    __LINE__, screen);
        }
    }
}

static void
test_no_duplicate_back_hints(void)
{
    static const indigo_screen screens[] = {
        INDIGO_SCREEN_SIGNIN,  INDIGO_SCREEN_HOME,       INDIGO_SCREEN_THREAD,
        INDIGO_SCREEN_PROFILE, INDIGO_SCREEN_NOTIFICATIONS, INDIGO_SCREEN_MENU,
        INDIGO_SCREEN_COMPOSE, INDIGO_SCREEN_SEARCH,     INDIGO_SCREEN_SETTINGS,
        INDIGO_SCREEN_UPDATE};
    indigo_app app;
    indigo_input in = {0};

    indigo_app_init(&app);
    for (unsigned s = 0; s < sizeof screens / sizeof screens[0]; s++) {
        app.screen = screens[s];
        check_hints_once(&app, &in, s);
    }
    /* A feed view is the one screen whose title is a name a person chose, and
     * its B leaves for the picker rather than the menu. */
    indigo_copy_utf8(app.feed_uri, sizeof app.feed_uri,
                     "at://did:plc:example/app.bsky.feed.xyz");
    indigo_copy_utf8(app.feed_name, sizeof app.feed_name, "Quiet posters");
    check_hints_once(&app, &in, INDIGO_SCREEN_HOME);
}

/* Nothing drawn may run off the edge of its screen: the 3DS cannot scroll a
 * status line back into view. Uses the same nominal character width the
 * layout wraps with, so it is a close bound rather than pixel truth. */
static void
check_text_on_screen(const indigo_app *app, const indigo_input *in, unsigned screen)
{
    indigo_canvas top;
    indigo_canvas bottom;
    const indigo_canvas *both[2];

    indigo_layout_build(app, in, &top, &bottom);
    both[0] = &top;
    both[1] = &bottom;
    for (int k = 0; k < 2; k++) {
        for (unsigned i = 0; i < both[k]->count; i++) {
            const indigo_cmd *cmd = &both[k]->cmds[i];
            float width;

            if (cmd->kind != INDIGO_CMD_TEXT) {
                continue;
            }
            width = (float) strlen(indigo_canvas_cmd_text(both[k], cmd)) *
                    INDIGO_CHAR_WIDTH * cmd->scale;
            s_checks++;
            if (cmd->x < 0.0f || cmd->y < 0.0f ||
                cmd->x + width > (float) both[k]->width ||
                cmd->y + INDIGO_CHAR_WIDTH * cmd->scale > (float) both[k]->height) {
                s_failures++;
                fprintf(stderr,
                        "%s:%d: text off screen on screen %u: x=%.1f y=%.1f w=%.1f "
                        "canvas=%dx%d |%s|\n",
                        __FILE__, __LINE__, screen, cmd->x, cmd->y, width,
                        both[k]->width, both[k]->height,
                        indigo_canvas_cmd_text(both[k], cmd));
            }
        }
    }
}

/* Shapes have to stay on screen too, and an image is the first thing Indigo
 * draws that is not text or a rect: an avatar can hang off the edge of a row
 * and nothing else in the layout would notice. */
static void
check_shapes_on_screen(const indigo_app *app, const indigo_input *in, unsigned screen)
{
    indigo_canvas top;
    indigo_canvas bottom;
    const indigo_canvas *both[2];

    indigo_layout_build(app, in, &top, &bottom);
    both[0] = &top;
    both[1] = &bottom;
    for (int k = 0; k < 2; k++) {
        for (unsigned i = 0; i < both[k]->count; i++) {
            const indigo_cmd *cmd = &both[k]->cmds[i];

            if (cmd->kind != INDIGO_CMD_IMAGE && cmd->kind != INDIGO_CMD_RECT) {
                continue;
            }
            s_checks++;
            if (cmd->x < 0.0f || cmd->y < 0.0f || cmd->w <= 0.0f || cmd->h <= 0.0f ||
                cmd->x + cmd->w > (float) both[k]->width ||
                cmd->y + cmd->h > (float) both[k]->height) {
                s_failures++;
                fprintf(stderr,
                        "%s:%d: shape off screen on screen %u: kind=%u x=%.1f y=%.1f "
                        "w=%.1f h=%.1f canvas=%dx%d\n",
                        __FILE__, __LINE__, screen, (unsigned) cmd->kind, cmd->x, cmd->y,
                        cmd->w, cmd->h, both[k]->width, both[k]->height);
            }
        }
    }
}

/* The image box of a post: what was drawn, where, and whether the box is the
 * shape the server said it was. A 4:3 photo drawn square is as wrong as one
 * drawn off the screen, and neither shows up in a text snapshot. */
static const indigo_cmd *
find_image(const indigo_canvas *c, const char *url)
{
    for (unsigned i = 0; i < c->count; i++) {
        if (c->cmds[i].kind == INDIGO_CMD_IMAGE &&
            strcmp(c->images[c->cmds[i].image_index].url, url) == 0) {
            return &c->cmds[i];
        }
    }
    return NULL;
}

/* Whether any text command on this canvas contains `needle`. */
static bool
text_has(const indigo_canvas *c, const char *needle)
{
    for (unsigned i = 0; i < c->count; i++) {
        const indigo_cmd *cmd = &c->cmds[i];

        if (cmd->kind != INDIGO_CMD_TEXT) {
            continue;
        }
        if (strstr(c->text + cmd->text_offset, needle)) {
            return true;
        }
    }
    return false;
}

/* How many lines of text the layout gave the post. The block is identified by
 * its own geometry rather than by a marker: the post's text is the only text at
 * the text column, at the post's scale, above the counters. A link card's title
 * is at a different inset and a card's own band. */
static unsigned
post_text_lines(const indigo_canvas *c)
{
    unsigned n = 0;

    for (unsigned i = 0; i < c->count; i++) {
        const indigo_cmd *cmd = &c->cmds[i];

        if (cmd->kind == INDIGO_CMD_TEXT && cmd->x == 18.0f && cmd->scale > 0.59f &&
            cmd->scale < 0.61f && cmd->y >= 90.0f && cmd->y < 200.0f) {
            n++;
        }
    }
    return n;
}

static void
test_layout_draws_post_images(void)
{
    static const char *const thumb =
        "https://cdn.example/img/feed_thumbnail/plain/did:plc:one/a@jpeg";
    indigo_app app;
    indigo_input in = {0};
    indigo_canvas top;
    indigo_canvas bottom;
    indigo_post *p;

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_HOME;
    app.timeline.count = 1;
    p = &app.timeline.posts[0];
    indigo_copy_utf8(p->text, sizeof p->text,
                     "Look at this. It is a photograph of a bridge, taken from "
                     "the towpath, on a day when the light was doing something "
                     "interesting to the water.");
    indigo_copy_utf8(p->embed_note, sizeof p->embed_note, "[1 image]");
    indigo_copy_utf8(p->embed_thumb, sizeof p->embed_thumb, thumb);
    p->embed_kind = INDIGO_EMBED_IMAGE;
    p->embed_w = 4;
    p->embed_h = 3;

    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(!top.overflow && !bottom.overflow);
    {
        const indigo_cmd *img = find_image(&top, thumb);

        CHECK(img != NULL);
        if (img) {
            /* The declared shape, inside the band, and no taller than the band
             * whatever the ratio says. */
            CHECK(fabsf(img->w - 80.0f) < 0.51f && fabsf(img->h - 60.0f) < 0.51f);
            CHECK(img->y >= 128.0f && img->y + img->h <= 194.0f);
            CHECK(img->x >= 0.0f && img->x + img->w <= (float) top.width);
            /* Centred in the band: equal space either side. */
            CHECK(fabsf(img->x - 18.0f - ((364.0f - img->w) / 2.0f)) < 0.51f);
            /* Requested at the size it is drawn at, so an avatar-sized decode
             * is not what a full box costs. */
            CHECK(fabsf((float) (img->w > img->h ? img->w : img->h) - 80.0f) < 0.51f);
        }
        /* The one-line note says less than the picture does, so a post whose
         * embed is drawn does not also print it. */
        CHECK(!text_has(&top, "[1 image]"));
                /* An image costs the post three of its five lines of text. */
        CHECK(post_text_lines(&top) == 2);
    }

    /* Portrait, square, and a wide panorama: each is fitted, none is stretched. */
    static const struct {
        unsigned w;
        unsigned h;
    } shapes[] = {{3, 4}, {1, 1}, {16, 9}, {9, 16}, {3, 1}, {1, 3}};
    for (unsigned i = 0; i < sizeof shapes / sizeof shapes[0]; i++) {
        const indigo_cmd *img;

        p->embed_w = (unsigned char) shapes[i].w;
        p->embed_h = (unsigned char) shapes[i].h;
        indigo_layout_build(&app, &in, &top, &bottom);
        img = find_image(&top, thumb);
        CHECK(img != NULL);
        if (!img) {
            continue;
        }
        /* The drawn ratio is the declared one to within a pixel of rounding,
         * and the box is inside the band whichever way it faces. */
        CHECK(fabsf(img->w / img->h - (float) shapes[i].w / (float) shapes[i].h) <
              0.06f);
        CHECK(img->h <= 60.51f && img->w <= 364.51f);
        CHECK(img->y >= 128.0f && img->y + img->h <= 194.0f);
        CHECK(img->x >= 17.0f && img->x + img->w <= 383.0f);
    }

    /* No declared ratio: a square, which is the assumption that distorts least
     * when the server says nothing at all. */
    p->embed_w = 0;
    p->embed_h = 0;
    indigo_layout_build(&app, &in, &top, &bottom);
    {
        const indigo_cmd *img = find_image(&top, thumb);

        CHECK(img != NULL);
        CHECK(img && fabsf(img->w - 60.0f) < 0.51f && fabsf(img->h - 60.0f) < 0.51f);
    }

    /* Four images, one drawn: the count is stated rather than implied. */
    p->embed_w = 4;
    p->embed_h = 3;
    p->embed_count = 4;
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(text_has(&top, "+3 more"));
    p->embed_count = 1;
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(!text_has(&top, "more"));

    /* An image the view could not give a URL for is not drawn: the note says
     * the post has an image, which is true, and the box would be a lie. The
     * text then gets its lines back, because nothing was drawn in their place. */
    p->embed_thumb[0] = '\0';
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(find_image(&top, thumb) == NULL);
    CHECK(top.image_count == 0);
    CHECK(text_has(&top, "[1 image]"));
    {
        unsigned full = post_text_lines(&top);

        CHECK(full > 2);
        CHECK(full <= 5);
    }
}

/* A video is drawn as its poster frame with a line saying it cannot play, and
 * the line sits under the poster rather than on it. */
static void
test_layout_draws_video_poster(void)
{
    static const char *const poster = "https://video.example/hls/did:plc:one/c/thumbnail.jpg";
    indigo_app app;
    indigo_input in = {0};
    indigo_canvas top;
    indigo_canvas bottom;
    indigo_post *p;
    const indigo_cmd *img;

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_HOME;
    app.timeline.count = 1;
    p = &app.timeline.posts[0];
    indigo_copy_utf8(p->text, sizeof p->text, "Watch this.");
    indigo_copy_utf8(p->embed_note, sizeof p->embed_note, "[video]");
    indigo_copy_utf8(p->embed_thumb, sizeof p->embed_thumb, poster);
    p->embed_kind = INDIGO_EMBED_VIDEO;
    p->embed_w = 16;
    p->embed_h = 9;

    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(!top.overflow);
    img = find_image(&top, poster);
    CHECK(img != NULL);
    CHECK(text_has(&top, "can't play"));
    CHECK(!text_has(&top, "[video]"));
    for (unsigned i = 0; i < top.count && img; i++) {
        if (top.cmds[i].kind == INDIGO_CMD_TEXT &&
            strstr(top.text + top.cmds[i].text_offset, "can't play")) {
            CHECK(top.cmds[i].y >= img->y + img->h);
        }
    }

    /* No poster URL, nothing to draw: the one-line note stands in for it. */
    p->embed_thumb[0] = '\0';
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(text_has(&top, "[video]"));
}

static void
test_layout_draws_link_cards(void)
{
    static const char *const card_thumb =
        "https://cdn.example/img/feed_thumbnail/plain/did:plc:one/card@jpeg";
    indigo_app app;
    indigo_input in = {0};
    indigo_canvas top;
    indigo_canvas bottom;
    indigo_post *p;
    unsigned rects;

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_HOME;
    app.timeline.count = 1;
    p = &app.timeline.posts[0];
    indigo_copy_utf8(p->text, sizeof p->text, "Worth reading, and long enough that the "
                                             "text wants more than the two lines an "
                                             "embed leaves it.");
    indigo_copy_utf8(p->embed_note, sizeof p->embed_note, "Link: A very long title");
    indigo_copy_utf8(p->embed_title, sizeof p->embed_title,
                     "The Bridges of the Tyne, one at a time");
    indigo_copy_utf8(p->embed_uri, sizeof p->embed_uri,
                     "https://example.com/bridges");
    p->embed_kind = INDIGO_EMBED_LINK;

    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(!top.overflow);
    CHECK(text_has(&top, "Bridges of the Tyne"));
    CHECK(text_has(&top, "example.com/bridges"));
    CHECK(!text_has(&top, "[Link:"));
    /* A card is a surface, so it is drawn as a rect where an image would be
     * drawn as an image. */
    rects = 0;
    for (unsigned i = 0; i < top.count; i++) {
        if (top.cmds[i].kind == INDIGO_CMD_RECT && top.cmds[i].y > 100.0f &&
            top.cmds[i].w > 300.0f) {
            rects++;
        }
    }
    CHECK(rects == 1);
    CHECK(top.image_count == 0);
    CHECK(post_text_lines(&top) == 2);
    /* The card's text and its URI both stay inside the card. */
    for (unsigned i = 0; i < top.count; i++) {
        const indigo_cmd *cmd = &top.cmds[i];

        if (cmd->kind == INDIGO_CMD_TEXT && cmd->y > 120.0f && cmd->y < 195.0f) {
            CHECK(cmd->x >= 18.0f);
            CHECK(cmd->x + cmd->w <= 382.0f);
        }
    }

    /* With a thumbnail the card shows it, and gives the title less width so
     * the two cannot collide. */
    unsigned title_w_with = 0;
    for (unsigned i = 0; i < top.count; i++) {
        if (top.cmds[i].kind == INDIGO_CMD_TEXT && strstr(top.text + top.cmds[i].text_offset,
                                                          "Bridges")) {
            title_w_with = (unsigned) (top.cmds[i].x + top.cmds[i].w);
        }
    }
    indigo_copy_utf8(p->embed_thumb, sizeof p->embed_thumb, card_thumb);
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(find_image(&top, card_thumb) != NULL);
    CHECK(text_has(&top, "Bridges"));
    {
        unsigned title_w_without = 0;

        for (unsigned i = 0; i < top.count; i++) {
            if (top.cmds[i].kind == INDIGO_CMD_TEXT &&
                strstr(top.text + top.cmds[i].text_offset, "Bridges")) {
                title_w_without = (unsigned) (top.cmds[i].x + top.cmds[i].w);
            }
        }
        CHECK(title_w_without > 0 && title_w_with > 0);
        /* The image is to the right of every line of title, and the title is
         * to the left of it: they do not overlap. */
        CHECK(title_w_without <= 322);
    }

    /* A link card with no title draws the URI as its title rather than an
     * empty card. */
    p->embed_thumb[0] = '\0';
    p->embed_title[0] = '\0';
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(text_has(&top, "example.com/bridges"));

    /* A card with no URI is not a card: it falls back to the note. */
    p->embed_uri[0] = '\0';
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(!text_has(&top, "bridges"));
    CHECK(text_has(&top, "Link: A very long title"));
    CHECK(post_text_lines(&top) > 2);
}

/* Alt text is carried on the post and drawn only when the setting is on. It
 * takes the lines it needs out of the bottom of the band, so the picture gets
 * what is left -- and never nothing. */
/* The text-size setting scales the post body and nothing else, keeps the block
 * inside the vertical budget the embed band is positioned against, and leaves
 * the default exactly as it was. */
static void
test_layout_text_scale(void)
{
    static const unsigned scales[] = {INDIGO_TEXT_SCALE_SMALL, INDIGO_TEXT_SCALE_NORMAL,
                                      INDIGO_TEXT_SCALE_LARGE};
    static const char *const body =
        "word word word word word word word word word word word word word word "
        "word word word word word word word word word word word word word word "
        "word word word word word word word word word word word word word word "
        "word word word word word word word word word word word word word word "
        "word word word word word word word word word word word word word word "
        "word word word word word word word word word word word word word word "
        "word word word word word word word word word word word word word word";
    float first_scale[3] = {0};
    unsigned lines[3] = {0};

    for (unsigned k = 0; k < 3; k++) {
        indigo_app app;
        indigo_input in = {0};
        indigo_canvas top;
        indigo_canvas bottom;

        indigo_app_init(&app);
        app.screen = INDIGO_SCREEN_HOME;
        app.timeline.count = 1;
        indigo_copy_utf8(app.timeline.posts[0].text, sizeof app.timeline.posts[0].text,
                         body);
        app.settings.text_scale = (indigo_text_scale) scales[k];
        indigo_layout_build(&app, &in, &top, &bottom);
        for (unsigned i = 0; i < top.count; i++) {
            const indigo_cmd *cmd = &top.cmds[i];

            if (cmd->kind == INDIGO_CMD_TEXT && strstr(top.text + cmd->text_offset, "word")) {
                if (lines[k]++ == 0) {
                    first_scale[k] = cmd->scale;
                }
                /* Never past the five-line block's bottom edge. */
                CHECK(cmd->y + cmd->h <= 98.0f + 5.0f * 19.0f + 1.0f);
            }
        }
    }
    CHECK(first_scale[0] < first_scale[1]);
    CHECK(first_scale[1] < first_scale[2]);
    CHECK(first_scale[1] > 0.599f && first_scale[1] < 0.601f); /* default unchanged */
    CHECK(lines[1] == 5);
    CHECK(lines[2] >= 1 && lines[2] <= lines[1]);
}

static void
test_layout_draws_alt_text(void)
{
    static const char *const thumb =
        "https://cdn.example/img/feed_thumbnail/plain/did:plc:one/a@jpeg";
    indigo_app app;
    indigo_input in = {0};
    indigo_canvas top;
    indigo_canvas bottom;
    indigo_post *p;
    float full_h;
    float alt_h;

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_HOME;
    app.timeline.count = 1;
    p = &app.timeline.posts[0];
    indigo_copy_utf8(p->text, sizeof p->text, "A photograph.");
    indigo_copy_utf8(p->embed_thumb, sizeof p->embed_thumb, thumb);
    p->embed_kind = INDIGO_EMBED_IMAGE;
    p->embed_w = 4;
    p->embed_h = 3;

    /* Off by default: nothing about the picture changes. */
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(!text_has(&top, "frost on the railings"));
    CHECK(find_image(&top, thumb) != NULL);
    full_h = find_image(&top, thumb)->h;

    /* A one-sentence description costs one line. */
    indigo_copy_utf8(p->embed_alt, sizeof p->embed_alt,
                     "The river at dawn, with frost on the railings.");
    app.settings.alt_text = true;
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(text_has(&top, "frost on the railings"));
    CHECK(find_image(&top, thumb) != NULL);
    alt_h = find_image(&top, thumb)->h;
    /* Smaller than it was, and still a box rather than a line. */
    CHECK(alt_h < full_h);
    CHECK(alt_h >= 29.0f);
    /* The picture and the description do not overlap: the picture's bottom is
     * above the first line of the description. */
    CHECK(find_image(&top, thumb)->y + alt_h <= 192.0f - 13.0f);

    /* A long description costs two lines, and the picture shrinks further
     * without disappearing. */
    indigo_copy_utf8(p->embed_alt, sizeof p->embed_alt,
                     "A long description that runs on past the two lines the band "
                     "has for it, so that the ellipsis is doing real work rather "
                     "than being decoration.");
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(text_has(&top, "..."));
    CHECK(find_image(&top, thumb) != NULL);
    CHECK(find_image(&top, thumb)->h < alt_h);
    CHECK(find_image(&top, thumb)->h >= 29.0f);

    /* The count label moves with the band rather than sitting under it. */
    p->embed_count = 4;
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(text_has(&top, "+3 more"));
    for (unsigned i = 0; i < top.count; i++) {
        if (top.cmds[i].kind == INDIGO_CMD_TEXT &&
            strstr(top.text + top.cmds[i].text_offset, "+3 more")) {
            CHECK(top.cmds[i].y + top.cmds[i].h <= 194.0f);
            CHECK(top.cmds[i].y >= find_image(&top, thumb)->y);
        }
    }

    /* Alt text on a post with a link card changes nothing: a link card's
     * picture has no alt text of its own to show. */
    p->embed_kind = INDIGO_EMBED_LINK;
    indigo_copy_utf8(p->embed_uri, sizeof p->embed_uri, "https://example.com/x");
    indigo_copy_utf8(p->embed_title, sizeof p->embed_title, "A link");
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(text_has(&top, "A link"));
    CHECK(!text_has(&top, "frost on the railings"));
}

/* The settings screen names the build it is: the tag, the build number and the
 * date, all three. This is the only place any of them is printed, so it is also
 * the only thing that keeps the Makefile's three stamped strings out of the
 * linker's reach -- a build with the header generated and nothing reading it
 * ships a binary that cannot say which build it is. */
static void
test_settings_screen_shows_the_build(void)
{
    indigo_app app;
    indigo_input in = {0};
    indigo_canvas top;
    indigo_canvas bottom;

    indigo_app_init(&app);
    indigo_app_open_settings(&app);
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(!top.overflow);
    CHECK(text_has(&top, INDIGO_BUILD_COMMIT));
    CHECK(text_has(&top, "build"));
    /* The host build has no generated header, so it falls back rather than
     * failing; the numbers are the ones the fallback uses. */
    if (strcmp(INDIGO_BUILD_COMMIT, "unknown") == 0) {
        CHECK(INDIGO_BUILD_NUMBER == 0);
        CHECK(text_has(&top, INDIGO_BUILD_DATE));
    }
    /* Every setting the bottom screen can change is also named on the top
     * screen. The alt-text row was added to one of them and not the other,
     * which is the kind of thing that only a test notices. */
    static const char *const labels[] = {
        "Theme",       "Text scale",  "Reduce motion", "High contrast",
        "Large touch targets", "Image alt text", "Diagnostics log", "Startup feed"};
    for (unsigned i = 0; i < sizeof labels / sizeof labels[0]; i++) {
        CHECK(text_has(&top, labels[i]));
    }
    CHECK(text_has(&bottom, "Image alt text"));
}

static void
test_shapes_stay_on_screen(void)
{
    static const indigo_screen screens[] = {
        INDIGO_SCREEN_SIGNIN,     INDIGO_SCREEN_HOME,      INDIGO_SCREEN_THREAD,
        INDIGO_SCREEN_PROFILE,    INDIGO_SCREEN_NOTIFICATIONS, INDIGO_SCREEN_MENU,
        INDIGO_SCREEN_COMPOSE,    INDIGO_SCREEN_SEARCH,    INDIGO_SCREEN_SETTINGS,
        INDIGO_SCREEN_UPDATE};
    static const char *const url =
        "https://cdn.bsky.app/img/avatar/plain/did:plc:rhiannon/avatar@jpeg";
    indigo_app app;
    indigo_input in = {0};

    indigo_app_init(&app);
    /* Every screen with a row, a header or a profile gets an avatar, because
     * the sweep is only worth anything against populated rows. */
    indigo_timeline_append(&app.timeline, &(indigo_post) {0});
    snprintf(app.timeline.posts[0].avatar, sizeof app.timeline.posts[0].avatar, "%s", url);
    indigo_copy_utf8(app.profile.avatar, sizeof app.profile.avatar, url);
    app.profile.loaded = true;
    indigo_copy_utf8(app.profile.display_name, sizeof app.profile.display_name, "Rhiannon");
    app.search.kind = INDIGO_SEARCH_PEOPLE;
    app.search.count = 1;
    indigo_copy_utf8(app.search.results.actors[0].avatar,
                     sizeof app.search.results.actors[0].avatar, url);
    app.search.results.actors[0].handle[0] = 'a';
    app.notifications.count = 1;
    snprintf(app.notifications.items[0].avatar, sizeof app.notifications.items[0].avatar,
             "%s", url);
    for (unsigned s = 0; s < sizeof screens / sizeof screens[0]; s++) {
        app.screen = screens[s];
        check_shapes_on_screen(&app, &in, s);
    }
}

static void
test_text_stays_on_screen(void)
{
    static const indigo_screen screens[] = {
        INDIGO_SCREEN_SIGNIN,     INDIGO_SCREEN_HOME,      INDIGO_SCREEN_THREAD,
        INDIGO_SCREEN_PROFILE,    INDIGO_SCREEN_NOTIFICATIONS, INDIGO_SCREEN_MENU,
        INDIGO_SCREEN_COMPOSE,    INDIGO_SCREEN_SEARCH,    INDIGO_SCREEN_SETTINGS,
        INDIGO_SCREEN_UPDATE};
    /* A feed name is the only title a person writes, and the bar beside it
     * holds a hint and the post counter, so its lengths are measured too: a
     * short one leaves the hint where every other screen keeps it, a long one
     * has to be cut rather than run over them. */
    static const char *const feed_names[] = {
        "Tech",
        "Quiet posters",
        "Quiet posters and writers",
        "A feed name long enough to need cutting somewhere in the middle of it",
    };
    indigo_app app;
    indigo_input in = {0};

    indigo_app_init(&app);
    for (unsigned s = 0; s < sizeof screens / sizeof screens[0]; s++) {
        app.screen = screens[s];
        check_text_on_screen(&app, &in, s);
    }
    indigo_copy_utf8(app.feed_uri, sizeof app.feed_uri,
                     "at://did:plc:example/app.bsky.feed.xyz");
    for (unsigned n = 0; n < sizeof feed_names / sizeof feed_names[0]; n++) {
        indigo_copy_utf8(app.feed_name, sizeof app.feed_name, feed_names[n]);
        /* A feed's name titles the home screen; say so rather than relying on
         * whichever screen the loop above happened to end on. */
        app.screen = INDIGO_SCREEN_HOME;
        check_text_on_screen(&app, &in, INDIGO_SCREEN_HOME);
    }
}

static void
test_search_model(void)
{
    indigo_search s;
    indigo_actor one;

    memset(&s, 0, sizeof s);
    /* Nothing to search for, and a search already running, both refuse. */
    CHECK(!indigo_search_can_submit(&s));
    indigo_copy_utf8(s.query, sizeof s.query, "alice");
    CHECK(indigo_search_can_submit(&s));
    s.loading = true;
    CHECK(!indigo_search_can_submit(&s));
    s.loading = false;

    /* Moving an empty list does nothing rather than selecting row 0. */
    CHECK(!indigo_search_move(&s, 1, INDIGO_TIMELINE_ROWS));
    CHECK(indigo_search_selected(&s) == NULL);
    CHECK(indigo_search_row(&s, 0, INDIGO_TIMELINE_ROWS) == NULL);

    memset(&one, 0, sizeof one);
    indigo_copy_utf8(one.handle, sizeof one.handle, "alice.example.com");
    s.results.actors[0] = one;
    s.count = 1;
    CHECK(indigo_search_selected(&s) == &s.results.actors[0]);

    /* The selection stops at both ends instead of running off the list. */
    CHECK(!indigo_search_move(&s, -1, INDIGO_TIMELINE_ROWS));
    CHECK(!indigo_search_move(&s, 1, INDIGO_TIMELINE_ROWS));
    CHECK(s.selected == 0);

    /* Rows outside the count are empty rather than stale. */
    CHECK(indigo_search_row(&s, 1, INDIGO_TIMELINE_ROWS) == NULL);
    CHECK(indigo_search_row(&s, 0, 0) == NULL);

    /* Clear forgets the results but keeps the typed query: reopening search
     * should not make the person type the name again. */
    indigo_search_clear(&s);
    CHECK(s.count == 0 && s.selected == 0 && s.scroll == 0);
    CHECK(!s.searched && !s.loading && s.status[0] == '\0');
    CHECK(strcmp(s.query, "alice") == 0);
}

/* The list scrolls to keep a long result set inside the visible rows. */
static void
test_search_selection_scroll(void)
{
    indigo_search s;
    char handle[32];

    memset(&s, 0, sizeof s);
    for (unsigned i = 0; i < INDIGO_SEARCH_MAX; i++) {
        snprintf(handle, sizeof handle, "person%02u.example.com", i);
        indigo_copy_utf8(s.results.actors[i].handle, sizeof s.results.actors[i].handle, handle);
        s.count++;
    }

    for (unsigned i = 0; i < INDIGO_SEARCH_ROWS; i++) {
        CHECK(indigo_search_move(&s, 1, INDIGO_SEARCH_ROWS));
    }
    CHECK(s.selected == INDIGO_SEARCH_ROWS);
    CHECK(s.scroll == 1);
    CHECK(indigo_search_row(&s, 0, INDIGO_SEARCH_ROWS) == &s.results.actors[1]);

    /* Already at the top: up does nothing rather than going negative. */
    s.selected = 0;
    s.scroll = 0;
    CHECK(!indigo_search_move(&s, -1, INDIGO_SEARCH_ROWS));
    CHECK(s.scroll == 0);
    CHECK(s.selected == 0);
}

static void
test_search_flow(void)
{
    indigo_app app;
    indigo_input in = {0};
    indigo_field f;
    indigo_rect r;
    indigo_actor actors[2] = {0};

    indigo_app_init(&app);
    /* Signing in lands on Home and asks for the first page; take it so the
     * menu is free to answer. */
    indigo_app_sign_in_succeeded(&app, "me.example.com");
    CHECK(app.screen == INDIGO_SCREEN_HOME);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_TIMELINE_REFRESH);

    /* Reached from the More menu, the same way Notifications is. */
    in.back = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_MENU);
    in = (indigo_input) {0};
    in.down = true;
    indigo_app_update(&app, &in);
    in = (indigo_input) {0};
    in.down = true;
    indigo_app_update(&app, &in);
    in = (indigo_input) {0};
    in.confirm = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_SEARCH);

    /* A opens the keyboard; the search itself is not started without a query. */
    in = (indigo_input) {0};
    in.confirm = true;
    indigo_app_update(&app, &in);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_EDIT_QUERY);

    /* Accepting the query runs the search in the same step. */
    indigo_app_set_query(&app, "alice");
    CHECK(indigo_app_peek_request(&app) == INDIGO_REQUEST_SEARCH);
    CHECK(strcmp(app.search.query, "alice") == 0);
    CHECK(app.search.loading);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_SEARCH);

    indigo_copy_utf8(actors[0].handle, sizeof actors[0].handle, "alice.example.com");
    indigo_copy_utf8(actors[0].display_name, sizeof actors[0].display_name, "Alice");
    indigo_copy_utf8(actors[1].handle, sizeof actors[1].handle, "alice2.example.com");
    indigo_app_search_loaded(&app, actors, 2, "");
    CHECK(app.search.count == 2);
    CHECK(app.search.searched);
    CHECK(!app.search.loading);

    /* SEL opens the selected person's profile, as it does on a thread. */
    in = (indigo_input) {0};
    in.refresh = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_PROFILE);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_PROFILE);
    CHECK(strcmp(app.request_post_uri, "alice.example.com") == 0);

    /* And back again returns to the results, not to Home. */
    in = (indigo_input) {0};
    in.back = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_SEARCH);
    CHECK(app.search.count == 2);

    /* Touch does the same two things the buttons do: the query box types, and
     * the SEL pill opens the profile. */
    r = indigo_layout_button_rect(INDIGO_ACTION_FIELD_QUERY);
    in = (indigo_input) {0};
    in.touch_pressed = true;
    in.touch_x = (int) (r.x + 4);
    in.touch_y = (int) (r.y + 4);
    indigo_app_update(&app, &in);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_EDIT_QUERY);

    r = indigo_layout_button_rect(INDIGO_ACTION_AUTHOR);
    in = (indigo_input) {0};
    in.touch_pressed = true;
    in.touch_x = (int) (r.x + 4);
    in.touch_y = (int) (r.y + 4);
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_PROFILE);
    CHECK(strcmp(app.request_post_uri, "alice.example.com") == 0);
}

/* A new query must not leave the previous results selectable. */
static void
test_search_query_resets_results(void)
{
    indigo_app app;
    indigo_actor actors[2] = {0};
    indigo_field f;

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_SEARCH;
    indigo_copy_utf8(actors[0].handle, sizeof actors[0].handle, "alice.example.com");
    indigo_copy_utf8(actors[1].handle, sizeof actors[1].handle, "alice2.example.com");
    indigo_app_set_query(&app, "alice");
    indigo_app_take_request(&app, &f);
    indigo_app_search_loaded(&app, actors, 2, "");
    CHECK(app.search.count == 2);

    indigo_app_set_query(&app, "bob");
    CHECK(app.search.count == 0);
    CHECK(indigo_search_selected(&app.search) == NULL);
    CHECK(!app.search.searched);
}

/* Nobody matching is a real outcome, distinct from not having searched. */
static void
test_search_empty_and_failure(void)
{
    indigo_app app;

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_SEARCH;
    CHECK(!app.search.searched && app.search.status[0] == '\0');

    indigo_search_begin_page(&app.search, false);
    indigo_app_search_loaded(&app, NULL, 0, "");
    CHECK(app.search.searched);
    CHECK(app.search.count == 0);
    CHECK(strcmp(app.search.status, "Nobody matched that.") == 0);
    CHECK(!app.search.status_is_error);

    indigo_app_search_failed(&app, "No connection.");
    CHECK(!app.search.loading);
    CHECK(app.search.status_is_error);
    CHECK(strcmp(app.search.status, "No connection.") == 0);
}

/* More results than the list can hold are dropped, not overflowed. */
static void
test_search_results_bounded(void)
{
    indigo_app app;
    indigo_actor many[INDIGO_SEARCH_MAX + 5];
    indigo_field f;

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_SEARCH;
    for (unsigned i = 0; i < sizeof many / sizeof many[0]; i++) {
        memset(&many[i], 0, sizeof many[i]);
        snprintf(many[i].handle, sizeof many[i].handle, "p%03u.example.com", i);
    }
    indigo_app_set_query(&app, "p");
    indigo_app_take_request(&app, &f);
    indigo_app_search_loaded(&app, many, sizeof many / sizeof many[0], "");
    CHECK(app.search.count == INDIGO_SEARCH_MAX);
}

/* Search pages append until the cursor runs out, the list fills, or a page
 * brings back nothing new. Each of those ends the paging cleanly. */
static void
test_search_paging(void)
{
    indigo_app app;
    indigo_actor page[2];

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_SEARCH;
    memset(page, 0, sizeof page);
    snprintf(page[0].handle, sizeof page[0].handle, "one.example.com");
    snprintf(page[1].handle, sizeof page[1].handle, "two.example.com");

    /* First page arrives with a cursor: more are wanted, but only when the
     * person scrolls near the end of what is held. */
    indigo_search_begin_page(&app.search, false);
    indigo_app_search_loaded(&app, page, 2, "cursor-1");
    CHECK(app.search.count == 2);
    CHECK(app.search.has_more);
    /* Two rows on a three-row screen sit inside the prefetch window, so the
     * next page is wanted straight away. */
    CHECK(indigo_search_wants_page(&app.search));
    app.search.selected = app.search.count - 1;
    CHECK(indigo_search_wants_page(&app.search));

    /* A second page appends rather than replacing. */
    indigo_search_begin_page(&app.search, true);
    indigo_app_search_loaded(&app, page, 2, "");
    CHECK(app.search.count == 4);
    CHECK(!app.search.has_more);
    CHECK(!indigo_search_wants_page(&app.search));

    /* An empty page on a non-empty list ends the paging even when the
     * server still offers a cursor. */
    indigo_search_begin_page(&app.search, true);
    indigo_app_search_loaded(&app, NULL, 0, "cursor-2");
    CHECK(app.search.count == 4);
    CHECK(!app.search.has_more);
}

/* Following is a toggle the server can disagree with, so it is tested in both
 * directions plus the failure that has to put the state back. */
static void
test_follow_toggle(void)
{
    indigo_app app;
    indigo_field f;

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_PROFILE;

    /* Nothing loaded means nothing to follow: the did is not known yet. */
    CHECK(indigo_layout_hit(INDIGO_SCREEN_PROFILE, 160, 70) == INDIGO_ACTION_FOLLOW);
    indigo_app_toggle_follow(&app);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_NONE);

    app.profile.loaded = true;
    snprintf(app.profile.did, sizeof app.profile.did, "did:plc:abc123");
    app.profile.followers = 12;

    /* Follow: flips at once rather than waiting for the network. */
    indigo_app_toggle_follow(&app);
    CHECK(app.profile.following);
    CHECK(app.profile.follow_busy);
    CHECK(app.request_follow);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_FOLLOW);

    /* The record URI is what makes a later unfollow possible at all. */
    indigo_app_follow_done(&app, true, "at://did:plc:abc/app.bsky.graph.follow/self/1");
    CHECK(!app.profile.follow_busy);
    CHECK(app.profile.following);
    CHECK(strcmp(app.profile.follow_uri,
                 "at://did:plc:abc/app.bsky.graph.follow/self/1") == 0);

    /* Unfollow: needs that URI, and clears it once the record is gone. */
    indigo_app_toggle_follow(&app);
    CHECK(!app.profile.following);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_FOLLOW);
    indigo_app_follow_done(&app, false, NULL);
    CHECK(!app.profile.following);
    CHECK(app.profile.follow_uri[0] == '\0');
}

static void
test_follow_failure_reverts(void)
{
    indigo_app app;
    indigo_field f;

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_PROFILE;
    app.profile.loaded = true;
    app.profile.followers = 3;

    indigo_app_toggle_follow(&app);
    CHECK(app.profile.following);
    indigo_app_take_request(&app, &f);

    /* A failed follow must not leave the UI claiming it happened. */
    indigo_app_follow_failed(&app, "No connection.");
    CHECK(!app.profile.following);
    CHECK(!app.profile.follow_busy);
    CHECK(app.profile.status_is_error);
    CHECK(strcmp(app.profile.status, "No connection.") == 0);

    /* And a failed unfollow has to put the following state back. */
    snprintf(app.profile.did, sizeof app.profile.did, "did:plc:abc123");
    snprintf(app.profile.follow_uri, sizeof app.profile.follow_uri,
             "at://did:plc:abc/app.bsky.graph.follow/self/1");
    app.profile.following = true;
    indigo_app_toggle_follow(&app);
    CHECK(!app.profile.following);
    indigo_app_take_request(&app, &f);
    indigo_app_follow_failed(&app, "Rate limited.");
    CHECK(app.profile.following);
    CHECK(strcmp(app.profile.follow_uri,
                 "at://did:plc:abc/app.bsky.graph.follow/self/1") == 0);
}

/* Following is one job at a time, and cannot start without a did. */
static void
test_follow_guards(void)
{
    indigo_app app;
    indigo_field f;

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_PROFILE;
    app.profile.loaded = true;

    /* Following with no did would reach the session and be refused there;
     * the app does not know that, so this only checks the busy guard. */
    app.profile.follow_busy = true;
    indigo_app_toggle_follow(&app);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_NONE);
    app.profile.follow_busy = false;

    /* Following but holding no record URI means unfollow has nothing to
     * delete, so the press is refused before any request is made. */
    app.profile.following = true;
    app.profile.follow_uri[0] = '\0';
    indigo_app_toggle_follow(&app);
    CHECK(app.profile.following);
    CHECK(!app.profile.follow_busy);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_NONE);
    CHECK(app.profile.status_is_error);
}

/* Muted words and hide-reposts. The list and the matching are Wolfram's (its own
 * vectors pin the rules); these check Indigo's side: the posts a mute is tried
 * against (text and tag facets), the loaded list, and the page filter. */
static void
test_prefs(void)
{
    indigo_prefs p;
    indigo_post post;
    const char *tags[] = {"Spoilers"};

    indigo_prefs_clear(&p);
    CHECK(!indigo_prefs_text_is_muted(&p, "anything", NULL, 0));
    CHECK(!indigo_prefs_text_is_muted(NULL, "anything", NULL, 0));

    CHECK(wf_muted_list_add(&p.muted, "cat", true, false, false, NULL));
    CHECK(wf_muted_list_add(&p.muted, "spoilers", false, true, false, NULL));
    CHECK(indigo_prefs_text_is_muted(&p, "I like my Cat.", NULL, 0));
    CHECK(!indigo_prefs_text_is_muted(&p, "a category of things", NULL, 0));
    /* A tag mute applies to the post's tag facets, not its text. */
    CHECK(indigo_prefs_text_is_muted(&p, "text", tags, 1));
    CHECK(!indigo_prefs_text_is_muted(&p, "spoilers in the text", NULL, 0));

    /* The same through a post: its tag facets are the tags. */
    memset(&post, 0, sizeof post);
    snprintf(post.text, sizeof post.text, "nothing to see");
    CHECK(!indigo_prefs_post_is_hidden(&p, &post, true));
    post.facet_count = 1;
    post.facets[0].kind = INDIGO_FACET_TAG;
    snprintf(post.facets[0].target, sizeof post.facets[0].target, "spoilers");
    CHECK(indigo_prefs_post_is_hidden(&p, &post, true));

    /* Loading from the server's preferences: the list, the home feed's
     * hide-reposts, and an expired mute left out. */
    {
        wf_actor_preferences src;
        wf_actor_pref_feed_view fv;
        wf_actor_pref_muted_word mw[2];
        char *targets[1] = {(char *) "content"};
        char home[] = "home";
        char dog[] = "dog";
        char old[] = "old";
        char past[] = "2020-01-01T00:00:00Z";

        memset(&src, 0, sizeof src);
        memset(&fv, 0, sizeof fv);
        memset(mw, 0, sizeof mw);
        fv.feed = home;
        fv.has_hide_reposts = true;
        fv.hide_reposts = true;
        src.feed_views = &fv;
        src.feed_view_count = 1;
        mw[0].value = dog;
        mw[0].targets = targets;
        mw[0].target_count = 1;
        mw[1].value = old;
        mw[1].targets = targets;
        mw[1].target_count = 1;
        mw[1].expires_at = past;
        src.muting_keywords = mw;
        src.muting_keyword_count = 2;

        indigo_prefs_from_wolfram(&p, &src, 1791055895);
        CHECK(p.hide_reposts);
        CHECK(indigo_prefs_text_is_muted(&p, "a dog", NULL, 0));
        CHECK(!indigo_prefs_text_is_muted(&p, "an old post", NULL, 0));
        indigo_prefs_from_wolfram(&p, NULL, 0);
        CHECK(!p.hide_reposts && p.muted.count == 0);
    }

    /* Page filtering: reposts hidden on the home timeline only, muted words
     * everywhere, posts before `from` untouched. */
    indigo_post page[4];

    memset(page, 0, sizeof page);
    for (unsigned i = 0; i < 4; i++) {
        snprintf(page[i].text, sizeof page[i].text, "post %u", i);
    }
    snprintf(page[1].text, sizeof page[1].text, "a BAN here");
    snprintf(page[2].reposted_by, sizeof page[2].reposted_by, "someone");

    indigo_prefs_clear(&p);
    p.hide_reposts = true;
    CHECK(wf_muted_list_add(&p.muted, "ban", true, false, false, NULL));

    CHECK(indigo_prefs_filter_page(&p, page, 4, 1, false) == 1);
    CHECK(page[1].reposted_by[0] != '\0');
    CHECK(strcmp(page[1].text, "post 2") == 0);
    CHECK(strcmp(page[2].text, "post 3") == 0);

    CHECK(indigo_prefs_filter_page(&p, page, 3, 0, true) == 1);
    CHECK(strcmp(page[0].text, "post 0") == 0);
    CHECK(strcmp(page[1].text, "post 3") == 0);

    /* Guards: null prefs hides nothing, and `from` past the end removes
     * nothing. */
    CHECK(indigo_prefs_filter_page(NULL, page, 3, 0, true) == 0);
    CHECK(indigo_prefs_filter_page(&p, page, 3, 3, true) == 0);
    CHECK(!indigo_prefs_post_is_hidden(&p, NULL, true));
}

/* Mute is a flag and block is a record, so the two settle differently: block
 * has to keep the URI an unblock deletes. */
static void
test_mute_block_toggle(void)
{
    indigo_app app;
    indigo_field f;

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_PROFILE;
    snprintf(app.profile.did, sizeof app.profile.did, "did:plc:abc123");
    app.profile.loaded = true;

    CHECK(indigo_layout_hit(INDIGO_SCREEN_PROFILE, 80, 118) == INDIGO_ACTION_MUTE);
    CHECK(indigo_layout_hit(INDIGO_SCREEN_PROFILE, 230, 118) == INDIGO_ACTION_BLOCK);

    indigo_app_toggle_mute(&app);
    CHECK(app.profile.muted && app.profile.mute_busy);
    CHECK(app.request_graph == INDIGO_GRAPH_MUTE);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_GRAPH);
    indigo_app_graph_done(&app, INDIGO_GRAPH_MUTE, NULL);
    CHECK(app.profile.muted && !app.profile.mute_busy);

    indigo_app_toggle_mute(&app);
    CHECK(!app.profile.muted);
    indigo_app_take_request(&app, &f);
    indigo_app_graph_done(&app, INDIGO_GRAPH_UNMUTE, NULL);
    CHECK(!app.profile.muted && !app.profile.mute_busy);

    /* A block reports the record it created; without it, unblock has nothing
     * to delete and the button refuses. */
    indigo_app_toggle_block(&app);
    CHECK(app.profile.blocked && app.profile.block_busy);
    CHECK(app.request_graph == INDIGO_GRAPH_BLOCK);
    indigo_app_take_request(&app, &f);
    indigo_app_graph_done(&app, INDIGO_GRAPH_BLOCK,
                          "at://did:plc:abc/app.bsky.graph.block/self/1");
    CHECK(app.profile.blocked && !app.profile.block_busy);
    CHECK(strcmp(app.profile.block_uri,
                 "at://did:plc:abc/app.bsky.graph.block/self/1") == 0);

    indigo_app_toggle_block(&app);
    CHECK(!app.profile.blocked);
    CHECK(app.request_graph == INDIGO_GRAPH_UNBLOCK);
    indigo_app_take_request(&app, &f);
    indigo_app_graph_done(&app, INDIGO_GRAPH_UNBLOCK, NULL);
    CHECK(!app.profile.blocked);
    CHECK(app.profile.block_uri[0] == '\0');
}

static void
test_mute_block_failure_reverts(void)
{
    indigo_app app;
    indigo_field f;

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_PROFILE;
    snprintf(app.profile.did, sizeof app.profile.did, "did:plc:abc123");
    app.profile.loaded = true;

    indigo_app_toggle_mute(&app);
    indigo_app_take_request(&app, &f);
    indigo_app_graph_failed(&app, INDIGO_GRAPH_MUTE, "No connection.");
    CHECK(!app.profile.muted);
    CHECK(!app.profile.mute_busy);
    CHECK(app.profile.status_is_error);

    /* A failed block must not leave a half-made block record behind. */
    indigo_app_toggle_block(&app);
    indigo_app_take_request(&app, &f);
    indigo_app_graph_failed(&app, INDIGO_GRAPH_BLOCK, "Rate limited.");
    CHECK(!app.profile.blocked);
    CHECK(!app.profile.block_busy);
    CHECK(app.profile.block_uri[0] == '\0');
}

/* Blocked but holding no URI cannot unblock, so the press is refused before
 * any request is made rather than failing at the server. */
static void
test_block_without_uri(void)
{
    indigo_app app;
    indigo_field f;

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_PROFILE;
    app.profile.loaded = true;
    app.profile.blocked = true;
    app.profile.block_uri[0] = '\0';

    indigo_app_toggle_block(&app);
    CHECK(app.profile.blocked);
    CHECK(!app.profile.block_busy);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_NONE);
    CHECK(app.profile.status_is_error);
}

/* One job at a time: a second press while one is in flight is ignored. */
static void
test_graph_guards(void)
{
    indigo_app app;
    indigo_field f;

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_PROFILE;
    app.profile.loaded = true;

    app.profile.mute_busy = true;
    indigo_app_toggle_mute(&app);
    CHECK(!app.profile.muted);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_NONE);
    app.profile.mute_busy = false;

    app.profile.block_busy = true;
    indigo_app_toggle_block(&app);
    CHECK(!app.profile.blocked);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_NONE);
}

/* Followers and following reuse the search screen, so what matters is that
 * they arrive with the right subject, drop the previous list, and refuse to
 * pretend a query box applies to them. */
static void
test_people_lists(void)
{
    indigo_app app;
    indigo_field f;
    indigo_actor a[3];

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_PROFILE;
    snprintf(app.profile.handle, sizeof app.profile.handle, "rhi.example.social");
    app.profile.loaded = true;
    app.profile.followers = 1204;
    app.profile.follows = 310;

    CHECK(indigo_layout_hit(INDIGO_SCREEN_PROFILE, 80, 143) == INDIGO_ACTION_FOLLOWERS);
    CHECK(indigo_layout_hit(INDIGO_SCREEN_PROFILE, 230, 143) == INDIGO_ACTION_FOLLOWING);

    indigo_app_open_people(&app, INDIGO_SEARCH_FOLLOWERS, app.profile.handle);
    CHECK(app.screen == INDIGO_SCREEN_SEARCH);
    CHECK(app.search.kind == INDIGO_SEARCH_FOLLOWERS);
    CHECK(strcmp(app.search.subject, "rhi.example.social") == 0);
    CHECK(app.search.loading);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_PEOPLE);

    /* A people list is not a search: no query box, no typing. */
    CHECK(!indigo_search_is_typed(&app.search));
    CHECK(strcmp(indigo_search_title(&app.search), "Followers") == 0);
    CHECK(strcmp(indigo_search_title(&(indigo_search) {.kind = INDIGO_SEARCH_FOLLOWING}),
           "Following") == 0);
    CHECK(strcmp(indigo_search_title(&(indigo_search) {.kind = INDIGO_SEARCH_PEOPLE}),
           "Search") == 0);

    /* Switching kind must not leave the previous list under the new heading. */
    memset(a, 0, sizeof a);
    snprintf(a[0].handle, sizeof a[0].handle, "one.example");
    indigo_app_search_loaded(&app, a, 1, "");
    CHECK(app.search.count == 1);
    indigo_app_open_people(&app, INDIGO_SEARCH_FOLLOWING, app.profile.handle);
    CHECK(app.search.count == 0);
    CHECK(app.search.loading);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_PEOPLE);

    /* Opening search from the menu resets the kind; indigo_search_clear is
     * the shared reset and must not clear the kind itself, or a people list
     * would silently become a person search. */
    indigo_search_clear(&app.search);
    CHECK(app.search.kind == INDIGO_SEARCH_FOLLOWING);
    CHECK(strcmp(indigo_search_title(&app.search), "Following") == 0);
}

/* Post search shares the screen with the people lists but not the result
 * type, so what matters is that the kind routes the request and that posts
 * are not read back through the actor accessors. */
static void
test_post_search(void)
{
    indigo_app app;
    indigo_field f;
    indigo_post p[2];

    /* Open the More menu the way the person does, then choose "Find posts",
     * which is the fourth row. */
    indigo_app_init(&app);
    indigo_app_sign_in_succeeded(&app, "ewancroft.uk");
    {
        indigo_rect mr = indigo_layout_button_rect(INDIGO_ACTION_MENU);
        indigo_input in = {.touch_pressed = true, .touch_x = (int) (mr.x + 4),
                           .touch_y = (int) (mr.y + 4)};

        indigo_app_update(&app, &in);
    }
    CHECK(app.screen == INDIGO_SCREEN_MENU);
    CHECK(app.menu.items[3].kind == INDIGO_MENU_FIND_POSTS);
    for (unsigned i = 0; i < 3; i++) {
        indigo_app_update(&app, &(indigo_input) {.down = true});
    }
    indigo_app_update(&app, &(indigo_input) {.confirm = true});
    CHECK(app.screen == INDIGO_SCREEN_SEARCH);
    CHECK(app.search.kind == INDIGO_SEARCH_POSTS);
    CHECK(indigo_search_is_posts(&app.search));
    CHECK(indigo_search_is_typed(&app.search));
    CHECK(strcmp(indigo_search_title(&app.search), "Post search") == 0);

    /* Signing in asks for the home timeline, and a search will not start
     * while a request is pending. */
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_TIMELINE_REFRESH);
    /* Submitting a post query asks for the post request, not the actor one. */
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_NONE);
    indigo_app_set_query(&app, "welsh borders");
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_POST_SEARCH);

    memset(p, 0, sizeof p);
    snprintf(p[0].uri, sizeof p[0].uri, "at://did:plc:a/app.bsky.feed.post/1");
    snprintf(p[0].text, sizeof p[0].text, "Rivers before roads.");
    snprintf(p[0].handle, sizeof p[0].handle, "rhi.example.social");
    snprintf(p[0].display_name, sizeof p[0].display_name, "Rhiannon");
    snprintf(p[1].uri, sizeof p[1].uri, "at://did:plc:a/app.bsky.feed.post/2");
    snprintf(p[1].text, sizeof p[1].text, "Old stones and newer roads.");
    indigo_app_post_search_loaded(&app, p, 2, "");
    CHECK(app.search.count == 2);
    CHECK(app.search.searched);
    CHECK(indigo_search_selected_post(&app.search) != NULL);
    CHECK(strcmp(indigo_search_selected_post(&app.search)->text, "Rivers before roads.") == 0);
    CHECK(indigo_search_row_post(&app.search, 0, INDIGO_SEARCH_ROWS) != NULL);

    /* "No posts matched" is distinct from not having searched. */
    indigo_search_begin_page(&app.search, false);
    indigo_app_post_search_loaded(&app, NULL, 0, "");
    CHECK(app.search.count == 0);
    CHECK(app.search.searched);
    CHECK(strcmp(app.search.status, "No posts matched that.") == 0);
}

/* A person's posts reuse the post list that post search fills, reached from
 * the profile. What matters is the subject travels with the request and the
 * previous list is dropped rather than relabelled. */
static void
test_author_posts(void)
{
    indigo_app app;
    indigo_field f;
    indigo_post p[1];

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_PROFILE;
    snprintf(app.profile.handle, sizeof app.profile.handle, "rhi.example.social");
    app.profile.loaded = true;

    CHECK(indigo_layout_hit(INDIGO_SCREEN_PROFILE, 80, 183) == INDIGO_ACTION_POSTS);

    indigo_app_open_author_posts(&app, app.profile.handle);
    CHECK(app.screen == INDIGO_SCREEN_SEARCH);
    CHECK(app.search.kind == INDIGO_SEARCH_AUTHOR);
    CHECK(indigo_search_is_posts(&app.search));
    /* No query box: there is nothing to type into an author's posts. */
    CHECK(!indigo_search_is_typed(&app.search));
    CHECK(strcmp(indigo_search_title(&app.search), "Posts") == 0);
    CHECK(app.search.loading);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_AUTHOR_FEED);

    memset(p, 0, sizeof p);
    snprintf(p[0].uri, sizeof p[0].uri, "at://did:plc:a/app.bsky.feed.post/7");
    snprintf(p[0].text, sizeof p[0].text, "Rivers before roads.");
    snprintf(p[0].handle, sizeof p[0].handle, "rhi.example.social");
    indigo_app_post_search_loaded(&app, p, 1, "");
    CHECK(app.search.count == 1);
    CHECK(!app.search.loading);
    CHECK(strcmp(indigo_search_selected_post(&app.search)->text, "Rivers before roads.") == 0);

    /* Opening someone else's posts drops the first person's. */
    indigo_app_open_author_posts(&app, "someone.else.example");
    CHECK(app.search.count == 0);
    CHECK(strcmp(app.search.subject, "someone.else.example") == 0);

    /* A tap on the header box moves to the next tab and refetches. Someone
     * else's tabs skip likes; the signed-in account's do not. */
    indigo_app_take_request(&app, &f);
    app.search.loading = false;
    CHECK(app.search.tab == WF_PROFILE_TAB_POSTS);
    indigo_app_next_author_tab(&app);
    CHECK(app.search.tab == WF_PROFILE_TAB_REPLIES);
    CHECK(strcmp(indigo_search_title(&app.search), "Replies") == 0);
    CHECK(app.search.loading);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_AUTHOR_FEED);
    app.search.loading = false;
    indigo_app_next_author_tab(&app);
    CHECK(app.search.tab == WF_PROFILE_TAB_MEDIA);
    app.search.loading = false;
    indigo_app_next_author_tab(&app);
    CHECK(app.search.tab == WF_PROFILE_TAB_POSTS);

    snprintf(app.signin.account, sizeof app.signin.account, "Me.Example");
    indigo_app_open_author_posts(&app, "me.example");
    app.search.tab = WF_PROFILE_TAB_MEDIA;
    app.search.loading = false;
    indigo_app_next_author_tab(&app);
    CHECK(app.search.tab == WF_PROFILE_TAB_LIKES);
    CHECK(strcmp(indigo_search_title(&app.search), "Likes") == 0);

    /* Opening a person afresh starts on their posts again. */
    indigo_app_open_author_posts(&app, "someone.else.example");
    CHECK(app.search.tab == WF_PROFILE_TAB_POSTS);
}

/* The pinned post costs no request: getProfile already returned the URI, and
 * opening it needs nothing more. */
static void
test_pinned_post(void)
{
    indigo_app app;
    indigo_field f;
    indigo_rect r;

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_PROFILE;
    snprintf(app.profile.handle, sizeof app.profile.handle, "rhi.example.social");
    app.profile.loaded = true;

    r = indigo_layout_button_rect(INDIGO_ACTION_PINNED);
    /* Nothing pinned yet: the row is still on screen but holds nothing to
     * open, so a touch there must not navigate anywhere. */
    app.profile.pinned_uri[0] = '\0';
    indigo_layout_hit(INDIGO_SCREEN_PROFILE, (int) (r.x + r.w / 2),
                      (int) (r.y + r.h / 2));
    CHECK(indigo_layout_hit(INDIGO_SCREEN_PROFILE, (int) (r.x + r.w / 2),
                            (int) (r.y + r.h / 2)) == INDIGO_ACTION_PINNED);

    /* Pinned: opening it requests the thread and no extra fetch of our own. */
    snprintf(app.profile.pinned_uri, sizeof app.profile.pinned_uri,
             "at://did:plc:abc/app.bsky.feed.post/3kqz9d2f7xw4");
    {
        indigo_input in = {.touch_pressed = true,
                           .touch_x = (int) (r.x + r.w / 2),
                           .touch_y = (int) (r.y + r.h / 2)};

        indigo_app_update(&app, &in);
    }
    CHECK(app.screen == INDIGO_SCREEN_THREAD);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_THREAD);
}

/* Curated lists reuse the search screen a third time: the lists themselves are
 * a third row type, and a list's members are people, so they reuse the actor
 * rows. What matters is the navigation chain stays honest -- a list row opens
 * members, a member row opens a profile -- and that opening a second list
 * drops the first's members rather than relabelling them. */
static void
test_lists(void)
{
    indigo_app app;
    indigo_field f;
    indigo_list lists[2];
    indigo_actor members[2];
    indigo_input in;
    indigo_rect r;

    indigo_app_init(&app);
    indigo_app_sign_in_succeeded(&app, "ewancroft.uk");
    /* Signing in asks for the timeline; take it so requests stay free. */
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_TIMELINE_REFRESH);

    /* Open the More menu the way the person does, then choose "Lists", which
     * sits between "Find posts" and "My profile". */
    r = indigo_layout_button_rect(INDIGO_ACTION_MENU);
    in = (indigo_input) {.touch_pressed = true, .touch_x = (int) (r.x + 4),
                         .touch_y = (int) (r.y + 4)};
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_MENU);
    CHECK(app.menu.items[4].kind == INDIGO_MENU_LISTS);
    /* Move to Lists (index 4) and confirm. */
    for (unsigned i = 0; i < 4; i++) {
        indigo_app_update(&app, &(indigo_input) {.down = true});
    }
    indigo_app_update(&app, &(indigo_input) {.confirm = true});
    CHECK(app.screen == INDIGO_SCREEN_SEARCH);
    CHECK(app.search.kind == INDIGO_SEARCH_LISTS);
    CHECK(indigo_search_is_lists(&app.search));
    CHECK(!indigo_search_is_typed(&app.search));
    CHECK(strcmp(indigo_search_title(&app.search), "Lists") == 0);
    CHECK(app.search.loading);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_LISTS);

    memset(lists, 0, sizeof lists);
    snprintf(lists[0].name, sizeof lists[0].name, "Rivers");
    snprintf(lists[0].description, sizeof lists[0].description,
             "People who walk the Welsh borders.");
    snprintf(lists[0].uri, sizeof lists[0].uri,
             "at://did:plc:me/app.bsky.graph.list/riverfolk");
    snprintf(lists[1].name, sizeof lists[1].name, "Stones");
    snprintf(lists[1].uri, sizeof lists[1].uri,
             "at://did:plc:me/app.bsky.graph.list/oldstones");
    indigo_app_lists_loaded(&app, lists, 2, "");
    CHECK(app.search.count == 2);
    CHECK(!app.search.loading);
    CHECK(strcmp(indigo_search_selected_list(&app.search)->name, "Rivers") == 0);
    CHECK(indigo_search_row_list(&app.search, 0, INDIGO_SEARCH_ROWS) != NULL);
    CHECK(indigo_search_row_list(&app.search, 2, INDIGO_SEARCH_ROWS) == NULL);

    /* SEL opens the selected list's members, carrying its name as the title. */
    in = (indigo_input) {.refresh = true};
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_SEARCH);
    CHECK(app.search.kind == INDIGO_SEARCH_LIST_MEMBERS);
    CHECK(strcmp(indigo_search_title(&app.search), "Members") == 0);
    CHECK(strcmp(app.search.subject, "Rivers") == 0);
    CHECK(app.search.loading);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_LIST_MEMBERS);
    CHECK(strcmp(app.request_list_uri, "at://did:plc:me/app.bsky.graph.list/riverfolk") == 0);

    /* Members are actors: the same loaded call and accessors as the people
     * lists, because the result type is the same. */
    memset(members, 0, sizeof members);
    snprintf(members[0].handle, sizeof members[0].handle, "rhi.example.social");
    snprintf(members[0].display_name, sizeof members[0].display_name, "Rhiannon");
    snprintf(members[1].handle, sizeof members[1].handle, "rhibear.example.social");
    indigo_app_search_loaded(&app, members, 2, "");
    CHECK(app.search.count == 2);
    CHECK(!app.search.loading);
    CHECK(strcmp(indigo_search_selected(&app.search)->handle, "rhi.example.social") == 0);

    /* SEL on a member opens their profile, not another list. */
    in = (indigo_input) {.refresh = true};
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_PROFILE);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_PROFILE);
    CHECK(strcmp(app.request_post_uri, "rhi.example.social") == 0);

    /* Back twice returns to the lists, which are still held: the lists array
     * is not cleared by viewing members. */
    indigo_app_update(&app, &(indigo_input) {.back = true});
    CHECK(app.screen == INDIGO_SCREEN_SEARCH);
    CHECK(app.search.kind == INDIGO_SEARCH_LIST_MEMBERS);
    indigo_app_update(&app, &(indigo_input) {.back = true});
    CHECK(app.screen == INDIGO_SCREEN_SEARCH);
    CHECK(app.search.kind == INDIGO_SEARCH_LISTS);
    CHECK(app.search.count == 2);
    CHECK(strcmp(indigo_search_selected_list(&app.search)->name, "Rivers") == 0);

    /* Opening a different list drops the first's members. */
    indigo_app_open_list_members(&app, lists[1].uri, lists[1].name);
    CHECK(app.search.kind == INDIGO_SEARCH_LIST_MEMBERS);
    CHECK(app.search.count == 0);
    CHECK(app.search.loading);
    CHECK(strcmp(app.search.subject, "Stones") == 0);
}

static void
test_feeds(void)
{
    indigo_app app;
    indigo_field f;
    indigo_list feeds[2];
    indigo_input in;

    indigo_app_init(&app);
    indigo_app_sign_in_succeeded(&app, "ewancroft.uk");
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_TIMELINE_REFRESH);

    /* "Feeds" sits between "Lists" and "My profile". */
    in = (indigo_input) {.touch_pressed = true};
    in.touch_x = 4;
    in.touch_y = 4;
    app.screen = INDIGO_SCREEN_HOME;
    {
        indigo_rect r = indigo_layout_button_rect(INDIGO_ACTION_MENU);

        in.touch_x = (int) (r.x + 4);
        in.touch_y = (int) (r.y + 4);
    }
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_MENU);
    CHECK(app.menu.items[5].kind == INDIGO_MENU_FEEDS);
    /* Move to Feeds (index 5) and confirm. */
    for (unsigned i = 0; i < 5; i++) {
        indigo_app_update(&app, &(indigo_input) {.down = true});
    }
    indigo_app_update(&app, &(indigo_input) {.confirm = true});
    CHECK(app.screen == INDIGO_SCREEN_SEARCH);
    CHECK(app.search.kind == INDIGO_SEARCH_FEEDS);
    CHECK(indigo_search_is_lists(&app.search));
    CHECK(strcmp(indigo_search_title(&app.search), "Feeds") == 0);
    CHECK(app.search.loading);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_FEEDS);

    memset(feeds, 0, sizeof feeds);
    snprintf(feeds[0].name, sizeof feeds[0].name, "Quiet posters");
    snprintf(feeds[0].uri, sizeof feeds[0].uri,
             "at://did:plc:me/app.bsky.feed.generator/quiet");
    snprintf(feeds[1].name, sizeof feeds[1].name, "Moon photos");
    snprintf(feeds[1].uri, sizeof feeds[1].uri,
             "at://did:plc:me/app.bsky.feed.generator/moon");
    indigo_app_feeds_loaded(&app, feeds, 2, "");
    CHECK(app.search.count == 2);
    CHECK(!app.search.loading);
    CHECK(strcmp(indigo_search_selected_list(&app.search)->name, "Quiet posters") == 0);

    /* SEL opens the feed on the home screen, and the request is the feed. */
    indigo_app_update(&app, &(indigo_input) {.refresh = true});
    CHECK(app.screen == INDIGO_SCREEN_HOME);
    CHECK(strcmp(app.feed_name, "Quiet posters") == 0);
    CHECK(app.timeline.loading);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_FEED);
    CHECK(strcmp(app.request_feed_uri, feeds[0].uri) == 0);

    /* Refresh on a feed view refreshes the feed, not the timeline. */
    indigo_timeline_finish_fetch(&app.timeline, "");
    indigo_app_update(&app, &(indigo_input) {.refresh = true});
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_FEED);

    /* B returns to the picker, and the home screen goes back to the
     * Following timeline. */
    indigo_timeline_finish_fetch(&app.timeline, "");
    indigo_app_update(&app, &(indigo_input) {.back = true});
    CHECK(app.screen == INDIGO_SCREEN_SEARCH);
    CHECK(app.search.kind == INDIGO_SEARCH_FEEDS);
    CHECK(app.feed_uri[0] == '\0');
    CHECK(app.timeline.loading);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_TIMELINE_REFRESH);

    /* Opening a different feed drops the first's posts. */
    indigo_timeline_finish_fetch(&app.timeline, "");
    app.search.selected = 1;
    indigo_app_update(&app, &(indigo_input) {.refresh = true});
    CHECK(app.screen == INDIGO_SCREEN_HOME);
    CHECK(strcmp(app.feed_name, "Moon photos") == 0);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_FEED);

    /* The bottom-right button does what B does: from a feed view it is the
     * picker, not the menu. */
    indigo_timeline_finish_fetch(&app.timeline, "");
    {
        indigo_rect r = indigo_layout_button_rect(INDIGO_ACTION_MENU);

        indigo_app_update(&app,
                          &(indigo_input) {.touch_pressed = true,
                                           .touch_x = (int) (r.x + 4),
                                           .touch_y = (int) (r.y + 4)});
    }
    CHECK(app.screen == INDIGO_SCREEN_SEARCH);
    CHECK(app.search.kind == INDIGO_SEARCH_FEEDS);
    CHECK(app.feed_uri[0] == '\0');
}

static void
test_settings_screen(void)
{
    indigo_app app;
    indigo_input in = {0};
    indigo_field f;

    indigo_app_init(&app);
    indigo_app_open_settings(&app);
    CHECK(app.screen == INDIGO_SCREEN_SETTINGS);
    CHECK(app.settings_selected == 0);

    /* Navigate down through settings options. */
    in.down = true;
    indigo_app_update(&app, &in);
    CHECK(app.settings_selected == 1);

    /* Toggle text scale with A button (NORMAL -> LARGE). */
    in = (indigo_input) {0};
    in.confirm = true;
    indigo_app_update(&app, &in);
    CHECK(app.settings.text_scale == INDIGO_TEXT_SCALE_LARGE);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_SAVE_SETTINGS);

    /* Toggle theme with touch on Row 0. */
    in = (indigo_input) {0};
    indigo_rect r = indigo_layout_button_rect(INDIGO_ACTION_SETTINGS_ROW0);
    in.touch_pressed = true;
    in.touch_x = (int) (r.x + 4);
    in.touch_y = (int) (r.y + 4);
    indigo_app_update(&app, &in);
    CHECK(app.settings_selected == 0);
    CHECK(app.settings.theme == INDIGO_THEME_LIGHT);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_SAVE_SETTINGS);

    /* The last settings row is reachable and toggles the setting it names,
     * rather than the diagnostics switch the row used to be. */
    in = (indigo_input) {0};
    in.touch_pressed = true;
    r = indigo_layout_button_rect(INDIGO_ACTION_SETTINGS_ROW5);
    in.touch_x = (int) (r.x + 4);
    in.touch_y = (int) (r.y + 4);
    indigo_app_update(&app, &in);
    CHECK(app.settings_selected == 5);
    CHECK(app.settings.alt_text);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_SAVE_SETTINGS);
    /* The rows after it kept their own settings: the new row is inserted, so a
     * diagnostics switch is still on row 6 and not somewhere new. */
    CHECK(app.settings.diagnostics);
    r = indigo_layout_button_rect(INDIGO_ACTION_SETTINGS_ROW6);
    in = (indigo_input) {0};
    in.touch_pressed = true;
    in.touch_x = (int) (r.x + 4);
    in.touch_y = (int) (r.y + 4);
    indigo_app_update(&app, &in);
    CHECK(app.settings_selected == 6);
    CHECK(!app.settings.diagnostics);
    CHECK(app.settings.alt_text);
    indigo_app_take_request(&app, &f);
    /* Down past the last row stays on the last row. */
    app.settings_selected = 7;
    in = (indigo_input) {0};
    in.down = true;
    indigo_app_update(&app, &in);
    CHECK(app.settings_selected == 7);
    /* Back to the first row for the rest of this test, which goes on to touch
     * just outside row 0. */
    app.settings_selected = 0;
    r = indigo_layout_button_rect(INDIGO_ACTION_SETTINGS_ROW0);

    /* Test large_targets touch target expansion: touch 2px outside normal rect. */
    app.settings.large_targets = true;
    in = (indigo_input) {0};
    in.touch_pressed = true;
    in.touch_x = (int) (r.x - 2);
    in.touch_y = (int) (r.y + 4);
    indigo_app_update(&app, &in);
    CHECK(app.settings_selected == 0);

    /* Exit settings back with B button. */
    in = (indigo_input) {0};
    in.back = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_SIGNIN);
}

/* --- images ------------------------------------------------------------- */

/* A fake decode result: the cache never looks inside the pixels, so a block of
 * bytes is enough to exercise ownership. Freed by the cache on eviction and on
 * clear, which is why these are malloc'd rather than static. */
static uint8_t *
fake_pixels(unsigned w, unsigned h)
{
    return malloc((size_t) w * h * 4u);
}

static void
test_canvas_image_dedupes_by_url(void)
{
    indigo_canvas c;

    indigo_canvas_init(&c, 100, 50);
    CHECK(indigo_canvas_image(&c, 0, 0, 8, 8, "https://cdn.example/a.jpg", 0x112233ff));
    CHECK(indigo_canvas_image(&c, 10, 0, 8, 8, "https://cdn.example/a.jpg", 0x112233ff));
    CHECK(indigo_canvas_image(&c, 20, 0, 8, 8, "https://cdn.example/b.jpg", 0));
    CHECK(c.image_count == 2);
    CHECK(c.cmds[0].kind == INDIGO_CMD_IMAGE && c.cmds[1].kind == INDIGO_CMD_IMAGE);
    /* The same avatar on two rows is one entry, and both commands point at it:
     * otherwise one screen's repeated avatar would exhaust the entry budget. */
    CHECK(c.cmds[0].image_index == 0 && c.cmds[1].image_index == 0);
    CHECK(c.cmds[2].image_index == 1);
    CHECK(strcmp(c.images[0].url, "https://cdn.example/a.jpg") == 0);
    CHECK(c.images[0].placeholder == 0x112233ff);
    CHECK(!c.overflow);
}

static void
test_canvas_image_refuses_unusable_urls(void)
{
    indigo_canvas c;
    char long_url[INDIGO_CANVAS_IMAGE_URL_MAX + 8];

    indigo_canvas_init(&c, 100, 50);
    CHECK(!indigo_canvas_image(&c, 0, 0, 8, 8, "", 0));
    CHECK(!indigo_canvas_image(&c, 0, 0, 8, 8, NULL, 0));
    CHECK(c.overflow);
    CHECK(c.count == 0 && c.image_count == 0);

    /* A URL the cache could not hold anyway: refused here rather than drawn
     * from a truncated key that would never match. */
    memset(long_url, 'a', sizeof long_url - 1);
    long_url[sizeof long_url - 1] = '\0';
    indigo_canvas_init(&c, 100, 50);
    CHECK(!indigo_canvas_image(&c, 0, 0, 8, 8, long_url, 0));
    CHECK(c.overflow && c.count == 0);
}

static void
test_canvas_image_entry_cap(void)
{
    indigo_canvas c;
    char url[64];

    indigo_canvas_init(&c, 400, 240);
    for (unsigned i = 0; i < INDIGO_CANVAS_MAX_IMAGES; i++) {
        snprintf(url, sizeof url, "https://cdn.example/%u.jpg", i);
        CHECK(indigo_canvas_image(&c, 0, 0, 4, 4, url, 0));
    }
    snprintf(url, sizeof url, "https://cdn.example/one-too-many.jpg");
    CHECK(!indigo_canvas_image(&c, 0, 0, 4, 4, url, 0));
    CHECK(c.overflow);
    CHECK(c.image_count == INDIGO_CANVAS_MAX_IMAGES);
}

static void
test_media_claim_and_publish(void)
{
    indigo_media_cache c;
    unsigned gen = 0;
    unsigned spare = 99;

    indigo_media_init(&c);
    CHECK(indigo_media_ready(&c, "https://cdn.example/a.jpg") == -1);

    CHECK(indigo_media_claim(&c, "https://cdn.example/a.jpg", 0, &gen) >= 0);
    CHECK(gen != 0);
    CHECK(c.slots[0].state == INDIGO_MEDIA_LOADING);
    CHECK(c.slots[0].generation == gen);
    CHECK(indigo_media_known(&c, "https://cdn.example/a.jpg"));
    /* A second claim for a URL already in flight would be a second fetch of
     * the same image, which is what a per-frame request turns into without
     * this. It also hands back no generation at all, so a caller cannot
     * publish against a claim that never happened. */
    CHECK(indigo_media_claim(&c, "https://cdn.example/a.jpg", 0, &spare) == -1);
    CHECK(spare == 0);

    CHECK(indigo_media_publish(&c, 0, gen, fake_pixels(8, 8), 8, 8));
    CHECK(c.slots[0].state == INDIGO_MEDIA_READY);
    CHECK(c.slots[0].width == 8 && c.slots[0].height == 8);
    CHECK(c.bytes == 8u * 8u * 4u);
    CHECK(indigo_media_ready(&c, "https://cdn.example/a.jpg") == 0);
    CHECK(c.hits == 1 && c.misses == 1);

    indigo_media_clear(&c);
    CHECK(c.bytes == 0);
    for (unsigned i = 0; i < INDIGO_MEDIA_SLOTS; i++) {
        CHECK(c.slots[i].state == INDIGO_MEDIA_EMPTY);
    }
    /* A clear frees what was resident and leaves the counters, which are
     * diagnostics rather than state. */
    CHECK(c.hits == 1 && c.loads == 1);
}

static void
test_media_rejects_stale_result(void)
{
    indigo_media_cache c;
    unsigned gen = 0;
    int slot;
    uint8_t *pixels;

    indigo_media_init(&c);
    slot = indigo_media_claim(&c, "https://cdn.example/a.jpg", 0, &gen);
    CHECK(slot >= 0);

    /* A result for the wrong generation is a fetch that finished after its
     * slot was reused. It must be refused, and the caller keeps its pixels. */
    pixels = fake_pixels(8, 8);
    CHECK(!indigo_media_publish(&c, slot, gen + 1, pixels, 8, 8));
    CHECK(c.slots[slot].state == INDIGO_MEDIA_LOADING);
    free(pixels);

    /* A slot index that was never claimed, and one out of range. */
    pixels = fake_pixels(8, 8);
    CHECK(!indigo_media_publish(&c, -1, gen, pixels, 8, 8));
    CHECK(!indigo_media_publish(&c, INDIGO_MEDIA_SLOTS, gen, pixels, 8, 8));
    free(pixels);

    /* A decoder that ignored its own cap must not be able to spend the
     * budget: the size check is the last line, not the first. It is the
     * thumbnail cap, not the avatar one -- a large image is legitimate, an
     * enormous one is not. */
    pixels = fake_pixels(8, 8);
    CHECK(!indigo_media_publish(&c, slot, gen, pixels, INDIGO_MEDIA_THUMB_DIM + 1, 8));
    free(pixels);

    CHECK(indigo_media_publish(&c, slot, gen, fake_pixels(8, 8), 8, 8));
    indigo_media_clear(&c);
}

static void
test_media_failed_url_is_not_refetched(void)
{
    indigo_media_cache c;
    unsigned gen = 0;
    int slot;

    indigo_media_init(&c);
    slot = indigo_media_claim(&c, "https://cdn.example/gone.jpg", 0, &gen);
    CHECK(slot >= 0);
    indigo_media_fail(&c, slot, gen);
    CHECK(c.slots[slot].state == INDIGO_MEDIA_FAILED);
    CHECK(c.failures == 1);
    CHECK(indigo_media_ready(&c, "https://cdn.example/gone.jpg") == -1);
    /* Still known, so the loader does not ask again every frame for a URL
     * that just failed. Only a clear makes it eligible. */
    CHECK(indigo_media_known(&c, "https://cdn.example/gone.jpg"));
    CHECK(indigo_media_claim(&c, "https://cdn.example/gone.jpg", 0, &gen) == -1);
    indigo_media_clear(&c);
    CHECK(indigo_media_claim(&c, "https://cdn.example/gone.jpg", 0, &gen) >= 0);
    indigo_media_clear(&c);

    /* A failure for a slot that is no longer in flight is ignored rather than
     * marking whatever took its place. */
    CHECK(indigo_media_claim(&c, "https://cdn.example/a.jpg", 0, &gen) >= 0);
    indigo_media_fail(&c, 0, gen + 99);
    CHECK(c.slots[0].state == INDIGO_MEDIA_LOADING);
    indigo_media_clear(&c);
}

static void
test_media_eviction_prefers_least_recently_used(void)
{
    indigo_media_cache c;
    unsigned gen = 0;
    int first;

    indigo_media_init(&c);
    /* Fill every slot, then touch them oldest-first so the one to go is
     * knowable. */
    for (unsigned i = 0; i < INDIGO_MEDIA_SLOTS; i++) {
        char url[64];

        snprintf(url, sizeof url, "https://cdn.example/%u.jpg", i);
        CHECK(indigo_media_claim(&c, url, 0, &gen) >= 0);
        CHECK(indigo_media_publish(&c, (int) i, gen, fake_pixels(8, 8), 8, 8));
    }
    for (unsigned i = 0; i < INDIGO_MEDIA_SLOTS; i++) {
        char url[64];

        snprintf(url, sizeof url, "https://cdn.example/%u.jpg", i);
        CHECK(indigo_media_ready(&c, url) == (int) i);
    }
    first = indigo_media_claim(&c, "https://cdn.example/new.jpg", 0, &gen);
    CHECK(first == 0);
    CHECK(c.evictions == 1);
    CHECK(c.slots[0].state == INDIGO_MEDIA_LOADING);
    CHECK(strcmp(c.slots[0].url, "https://cdn.example/new.jpg") == 0);
    /* The byte total never doubles: the evicted image's bytes came off before
     * the new one went on. */
    CHECK(c.bytes == (unsigned) INDIGO_MEDIA_SLOTS * 8u * 8u * 4u - 8u * 8u * 4u);
    indigo_media_clear(&c);
    CHECK(c.bytes == 0);
}

static void
test_media_never_evicts_in_flight(void)
{
    indigo_media_cache c;
    unsigned gen = 0;

    indigo_media_init(&c);
    for (unsigned i = 0; i < INDIGO_MEDIA_SLOTS; i++) {
        char url[64];

        snprintf(url, sizeof url, "https://cdn.example/%u.jpg", i);
        CHECK(indigo_media_claim(&c, url, 0, &gen) >= 0);
    }
    /* Every slot is fetching. Taking one would throw away a request already
     * paid for, so the claim is refused and the caller carries on without an
     * image. */
    CHECK(indigo_media_claim(&c, "https://cdn.example/one-too-many.jpg", 0, &gen) == -1);
    indigo_media_clear(&c);
}

static void
test_media_byte_budget(void)
{
    indigo_media_cache c;
    unsigned gens[INDIGO_MEDIA_SLOTS];
    const unsigned dim = INDIGO_MEDIA_THUMB_DIM;
    const unsigned per_image = dim * dim * 4u;
    unsigned at_cap = 0;

    /* The test uses the decode cap a thumbnail asks for, because a budget that
     * fits every slot at the avatar size would never be reached and would
     * prove nothing. */
    CHECK(per_image * (INDIGO_MEDIA_BYTES_MAX / per_image + 1) > INDIGO_MEDIA_BYTES_MAX);
    CHECK(INDIGO_MEDIA_THUMB_DIM * INDIGO_MEDIA_THUMB_DIM * 4u *
              INDIGO_MEDIA_SLOTS > INDIGO_MEDIA_BYTES_MAX);

    indigo_media_init(&c);
    /* Claim every slot first, so eviction has to make room at publish time
     * rather than at claim time: the decoded size is not known until then.
     * Every claim hands out its own generation, so they are kept. */
    for (unsigned i = 0; i < INDIGO_MEDIA_SLOTS; i++) {
        char url[64];

        snprintf(url, sizeof url, "https://cdn.example/%u.jpg", i);
        CHECK(indigo_media_claim(&c, url, 0, &gens[i]) >= 0);
    }
    for (unsigned i = 0; i < INDIGO_MEDIA_SLOTS; i++) {
        uint8_t *pixels = fake_pixels(dim, dim);

        if (indigo_media_publish(&c, (int) i, gens[i], pixels, dim, dim)) {
            at_cap++;
        } else {
            /* Over budget with nothing left to evict: the pixels are still the
             * caller's to free. */
            free(pixels);
        }
    }
    CHECK(c.bytes <= INDIGO_MEDIA_BYTES_MAX);
    CHECK(c.bytes % per_image == 0);
    /* Every publish succeeded, because each one evicted an older image to make
     * room: the decode sizes are the same, so the resident count is what the
     * budget actually caps, and it is fewer than the slots. That is what makes
     * the budget a real bound rather than a number that never bites. */
    CHECK(c.evictions > 0);
    CHECK(c.bytes / per_image < INDIGO_MEDIA_SLOTS);
    CHECK(at_cap == INDIGO_MEDIA_SLOTS);
    /* The most recent images are the ones kept, and the oldest went first. */
    CHECK(indigo_media_ready(&c, "https://cdn.example/0.jpg") < 0);
    CHECK(indigo_media_ready(&c, "https://cdn.example/23.jpg") >= 0);
    indigo_media_clear(&c);
    CHECK(c.bytes == 0);
}

static void
test_media_claim_guards(void)
{
    indigo_media_cache c;
    unsigned gen = 1234;
    char long_url[INDIGO_MEDIA_URL_MAX + 8];

    indigo_media_init(&c);
    CHECK(indigo_media_claim(&c, "", 0, &gen) == -1);
    CHECK(indigo_media_claim(&c, NULL, 0, &gen) == -1);
    CHECK(indigo_media_claim(&c, "https://cdn.example/a.jpg", 0, NULL) == -1);
    CHECK(indigo_media_ready(&c, "") == -1);
    CHECK(!indigo_media_known(&c, ""));
    CHECK(!indigo_media_known(&c, NULL));

    /* One byte over what the cache stores is refused rather than truncated: a
     * truncated key would never match the URL the layout asks for again. */
    memset(long_url, 'b', sizeof long_url - 1);
    long_url[sizeof long_url - 1] = '\0';
    CHECK(indigo_media_claim(&c, long_url, 0, &gen) == -1);
    CHECK(indigo_media_ready(&c, long_url) == -1);
}

static void
test_media_placeholder_colour(void)
{
    uint32_t a = indigo_media_placeholder_color("https://cdn.example/alice.jpg");
    uint32_t b = indigo_media_placeholder_color("https://cdn.example/alice.jpg");
    uint32_t other = indigo_media_placeholder_color("https://cdn.example/bob.jpg");

    CHECK(a == b);
    CHECK((a & 0xff) == 255);
    /* Two accounts must be able to look different: a column of identical
     * placeholders reads as one voice. */
    CHECK(a != other);
    /* And the ramp has to be wide enough for that to hold in general. A hue
     * step with a binary choice inside each sector produces six colours, and
     * two accounts collide on one often enough to defeat the point. */
    {
        uint32_t seen[64];
        char url[64];
        unsigned distinct = 0;

        for (unsigned i = 0; i < 64; i++) {
            unsigned duplicate = 0;

            snprintf(url, sizeof url, "https://cdn.example/%u.jpg", i);
            seen[i] = indigo_media_placeholder_color(url);
            for (unsigned k = 0; k < distinct; k++) {
                duplicate += seen[k] == seen[i];
            }
            if (!duplicate) {
                seen[distinct++] = seen[i];
            }
        }
        /* Some collisions are expected -- 64 hashes into 360 hues collide by
         * the birthday bound, and two accounts sharing a placeholder is
         * harmless. What has to hold is that the ramp is a ramp: picking a
         * binary position inside each 60-degree sector gives six colours for
         * the whole of the wheel, and that is far too few to tell a column of
         * accounts apart. */
        CHECK(distinct >= 40);
    }
    /* Deterministic across processes, so a placeholder does not change colour
     * between two frames of the same screen. */
    CHECK(indigo_media_placeholder_color("") == indigo_media_placeholder_color(""));
}

static void
test_layout_draws_avatars(void)
{
    indigo_app app;
    indigo_input in = {0};
    indigo_canvas top;
    indigo_canvas bottom;
    const char *url = "https://cdn.example/img/avatar/plain/did:plc:one/avatar@jpeg";
    unsigned top_images = 0;
    unsigned bottom_images = 0;

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_HOME;
    snprintf(app.timeline.posts[0].avatar, sizeof app.timeline.posts[0].avatar, "%s", url);
    indigo_copy_utf8(app.timeline.posts[0].display_name,
                     sizeof app.timeline.posts[0].display_name, "Ada");
    app.timeline.count = 1;

    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(!top.overflow && !bottom.overflow);
    for (unsigned i = 0; i < top.count; i++) {
        top_images += top.cmds[i].kind == INDIGO_CMD_IMAGE ? 1u : 0u;
    }
    for (unsigned i = 0; i < bottom.count; i++) {
        bottom_images += bottom.cmds[i].kind == INDIGO_CMD_IMAGE ? 1u : 0u;
    }
    /* The selected post's avatar on the top screen, and the same avatar on the
     * row for it on the bottom screen. Both name the same URL, so they share
     * one entry per canvas. */
    CHECK(top_images == 1);
    CHECK(bottom_images == 1);
    CHECK(top.image_count == 1);
    CHECK(strcmp(top.images[0].url, url) == 0);
    /* The placeholder is part of the command, so both backends draw the same
     * thing before the pixels arrive. */
    CHECK(top.cmds[0].kind != INDIGO_CMD_IMAGE || top.images[0].placeholder != 0);

    /* With no avatar on the post, no image command is emitted at all: an
     * account that has set no picture is not a missing picture. */
    app.timeline.posts[0].avatar[0] = '\0';
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(top.image_count == 0);
    CHECK(bottom.image_count == 0);

    /* The profile and the people rows carry avatars too. */
    indigo_copy_utf8(app.profile.avatar, sizeof app.profile.avatar, url);
    app.profile.loaded = true;
    indigo_copy_utf8(app.profile.display_name, sizeof app.profile.display_name, "Ada");
    app.screen = INDIGO_SCREEN_PROFILE;
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(top.image_count == 1);

    app.screen = INDIGO_SCREEN_SEARCH;
    app.search.kind = INDIGO_SEARCH_PEOPLE;
    app.search.count = 1;
    app.search.searched = true;
    indigo_copy_utf8(app.search.results.actors[0].handle,
                     sizeof app.search.results.actors[0].handle, "ada.test");
    indigo_copy_utf8(app.search.results.actors[0].avatar,
                     sizeof app.search.results.actors[0].avatar, url);
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(bottom.image_count == 1);
}

/* ---------------------------------------------------------------------------
 * The full-size image viewer
 * ------------------------------------------------------------------------- */

/* A post with a picture on it, the way the appview describes one: a kind, a
 * declared aspect, a CDN thumbnail URL and the author's description. */
static void
with_image(indigo_post *p, unsigned w, unsigned h, unsigned count, const char *alt)
{
    p->embed_kind = INDIGO_EMBED_IMAGE;
    p->embed_w = (unsigned char) w;
    p->embed_h = (unsigned char) h;
    p->embed_count = (unsigned char) count;
    snprintf(p->embed_thumb, sizeof p->embed_thumb,
             "https://cdn.bsky.app/img/feed_thumbnail/plain/did:plc:one/river@jpeg");
    indigo_copy_utf8(p->embed_alt, sizeof p->embed_alt, alt);
}

static void
test_image_viewer_opens_from_the_selected_post(void)
{
    indigo_app app;
    indigo_input in = {0};
    indigo_post p = make_post("at://a/app.bsky.feed.post/1", "a river");

    with_image(&p, 3, 4, 4, "The river at dawn.");
    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_THREAD;
    app.thread_focus = 0;
    CHECK(indigo_timeline_append(&app.thread, &p));
    app.thread.selected = 0;

    CHECK(indigo_app_open_image(&app));
    CHECK(app.screen == INDIGO_SCREEN_IMAGE);
    CHECK(strcmp(app.image.url, p.embed_thumb) == 0);
    CHECK(strcmp(app.image.alt, "The river at dawn.") == 0);
    CHECK(app.image.aspect_w == 3 && app.image.aspect_h == 4);
    CHECK(app.image.count == 4);

    /* B leaves, and leaves for the screen it was opened from rather than for
     * Home: the viewer is pushed like any other screen. */
    in.back = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_THREAD);
}

static void
test_image_viewer_needs_an_image(void)
{
    indigo_app app;
    indigo_post plain = make_post("at://a/app.bsky.feed.post/1", "no picture");
    indigo_post card = make_post("at://a/app.bsky.feed.post/2", "a link");

    card.embed_kind = INDIGO_EMBED_LINK;
    indigo_copy_utf8(card.embed_uri, sizeof card.embed_uri, "https://example.com");

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_THREAD;
    CHECK(indigo_timeline_append(&app.thread, &plain));
    app.thread.count = 1;

    /* Nothing to open, and the screen must not change: a link card and a post
     * with no embed are both things the viewer cannot show. */
    CHECK(!indigo_app_open_image(&app));
    CHECK(app.screen == INDIGO_SCREEN_THREAD);

    /* An image post with no URL is the third way to have nothing to show, and
     * the reason this is about the pair rather than the kind alone. */
    app.thread.posts[0] = card;
    with_image(&app.thread.posts[0], 4, 3, 1, "");
    app.thread.posts[0].embed_thumb[0] = '\0';
    CHECK(!indigo_app_open_image(&app));
    CHECK(app.screen == INDIGO_SCREEN_THREAD);

    /* And with no post selected at all. */
    app.thread.count = 0;
    app.thread.selected = 0;
    CHECK(!indigo_app_open_image(&app));
}

static void
test_image_viewer_holds_a_copy(void)
{
    indigo_app app;
    indigo_post p = make_post("at://a/app.bsky.feed.post/1", "a river");

    with_image(&p, 16, 9, 1, "");
    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_THREAD;
    CHECK(indigo_timeline_append(&app.thread, &p));

    CHECK(indigo_app_open_image(&app));
    /* The list it came from keeps paging and will reuse that slot, so the
     * viewer cannot be looking at it. */
    indigo_timeline_clear(&app.thread);
    CHECK(strcmp(app.image.url, p.embed_thumb) == 0);
    CHECK(app.screen == INDIGO_SCREEN_IMAGE);
}

static void
test_image_viewer_from_search_results(void)
{
    indigo_app app;
    indigo_post p = make_post("at://a/app.bsky.feed.post/1", "a river");

    with_image(&p, 1, 1, 1, "");
    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_SEARCH;
    app.search.kind = INDIGO_SEARCH_POSTS;
    app.search.results.posts[0] = p;
    app.search.count = 1;

    CHECK(indigo_app_open_image(&app));
    CHECK(app.screen == INDIGO_SCREEN_IMAGE);

    /* An actor row has no picture, and opening one from it has to be a no-op
     * rather than reaching into the union and reading a name as a URL. */
    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_SEARCH;
    app.search.kind = INDIGO_SEARCH_FOLLOWERS;
    indigo_copy_utf8(app.search.results.actors[0].handle, sizeof app.search.results.actors[0].handle,
                     "rhi.example.social");
    app.search.count = 1;
    CHECK(!indigo_app_open_image(&app));
    CHECK(app.screen == INDIGO_SCREEN_SEARCH);
}

static void
test_image_button_is_offered_only_when_there_is_an_image(void)
{
    indigo_app app;
    indigo_input in = {0};
    indigo_canvas top;
    indigo_canvas bottom;
    indigo_rect image = indigo_layout_button_rect(INDIGO_ACTION_IMAGE);
    indigo_post p = make_post("at://a/app.bsky.feed.post/1", "a river");
    int x = (int) (image.x + image.w / 2);
    int y = (int) (image.y + image.h / 2);

    with_image(&p, 3, 4, 1, "");
    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_THREAD;
    CHECK(indigo_timeline_append(&app.thread, &p));

    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(text_has(&bottom, "Image"));
    CHECK(indigo_layout_hit(INDIGO_SCREEN_THREAD, x, y) == INDIGO_ACTION_IMAGE);

    /* With no picture to open the button is gone from the screen, which is the
     * part a thumb can see. The rectangle stays in the screen's action list --
     * hit testing is per screen and does not know what is selected -- so what
     * has to refuse is the handler, and that is checked where the handler is. */
    app.thread.posts[0].embed_thumb[0] = '\0';
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(!text_has(&bottom, "Image"));
    CHECK(indigo_app_image_source(&app) == NULL || !indigo_post_has_image(
                                                      indigo_app_image_source(&app)));

    /* Moving the selection to a post with a picture brings the button back,
     * because it follows the selection rather than the screen. */
    app.thread.posts[1] = p;
    app.thread.count = 2;
    app.thread.selected = 1;
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(text_has(&bottom, "Image"));

    /* And the timeline and a post result offer it too, since both draw the
     * selected post's picture. */
    app.screen = INDIGO_SCREEN_HOME;
    app.timeline.posts[0] = p;
    app.timeline.count = 1;
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(text_has(&bottom, "Image"));

    app.screen = INDIGO_SCREEN_SEARCH;
    app.search.kind = INDIGO_SEARCH_POSTS;
    app.search.results.posts[0] = p;
    app.search.count = 1;
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(text_has(&bottom, "Image"));
}

static void
test_image_button_keeps_the_header_bar_readable(void)
{
    indigo_rect image = indigo_layout_button_rect(INDIGO_ACTION_IMAGE);
    indigo_rect back = indigo_layout_button_rect(INDIGO_ACTION_BACK);

    CHECK(!overlap(image, back));
    CHECK(image.x + image.w <= INDIGO_BOTTOM_WIDTH);
    CHECK(image.y + image.h <= INDIGO_BOTTOM_HEIGHT);
    /* Clear of the longest title the header bar draws at its own scale. The
     * bar is the one place on this screen with no room to be generous, and a
     * button drawn over the screen's own name is two controls in one place. */
    CHECK(image.x >= 14.0f + 8.0f * INDIGO_CHAR_WIDTH * 0.75f);
    /* Tall enough to hit, like every other button here. */
    CHECK(image.h >= 34.0f);
}

static void
test_image_viewer_draws_the_picture_at_the_screen(void)
{
    indigo_app app;
    indigo_input in = {0};
    indigo_canvas top;
    indigo_canvas bottom;
    const indigo_cmd *cmd;
    static const struct {
        unsigned w;
        unsigned h;
        float expect_w;
        float expect_h;
    } shapes[] = {
        {3, 4, 180.0f, 240.0f},  /* portrait: the height is the limit */
        {16, 9, 400.0f, 225.0f}, /* landscape: the width is */
        {1, 1, 240.0f, 240.0f},  /* square: the height decides */
        {0, 0, 240.0f, 240.0f},  /* undeclared: treated as square */
    };

    for (unsigned i = 0; i < sizeof shapes / sizeof shapes[0]; i++) {
        indigo_post p = make_post("at://a/app.bsky.feed.post/1", "a river");

        with_image(&p, shapes[i].w, shapes[i].h, 1, "");
        indigo_app_init(&app);
        app.screen = INDIGO_SCREEN_THREAD;
        CHECK(indigo_timeline_append(&app.thread, &p));
        CHECK(indigo_app_open_image(&app));

        indigo_layout_build(&app, &in, &top, &bottom);
        CHECK(!top.overflow);
        cmd = find_image(&top, p.embed_thumb);
        CHECK(cmd != NULL);
        if (!cmd) {
            continue;
        }
        /* The declared shape, fitted inside the screen and centred in it. A
         * box outside the screen is a box the reader cannot see, and a box
         * stretched to something else is not the photograph. */
        CHECK(fabsf(cmd->w - shapes[i].expect_w) < 0.5f);
        CHECK(fabsf(cmd->h - shapes[i].expect_h) < 0.5f);
        CHECK(fabsf(cmd->x - (INDIGO_TOP_WIDTH - cmd->w) / 2.0f) < 0.5f);
        CHECK(fabsf(cmd->y - (INDIGO_TOP_HEIGHT - cmd->h) / 2.0f) < 0.5f);
        CHECK(cmd->x >= 0.0f && cmd->y >= 0.0f);
        CHECK(cmd->x + cmd->w <= (float) INDIGO_TOP_WIDTH);
        CHECK(cmd->y + cmd->h <= (float) INDIGO_TOP_HEIGHT);
        /* Nothing else on the picture's screen: a viewer with a header on it
         * is a list row. The one command besides the picture is the
         * background behind it. */
        {
            unsigned images = 0;

            for (unsigned k = 0; k < top.count; k++) {
                images += top.cmds[k].kind == INDIGO_CMD_IMAGE ? 1u : 0u;
            }
            CHECK(images == 1);
            CHECK(top.count == 2);
            CHECK(top.cmds[0].kind == INDIGO_CMD_RECT);
            CHECK(!text_has(&top, "Image"));
        }
    }
}

static void
test_image_viewer_says_what_the_picture_is(void)
{
    indigo_app app;
    indigo_input in = {0};
    indigo_canvas top;
    indigo_canvas bottom;
    indigo_post p = make_post("at://a/app.bsky.feed.post/1", "a river");

    with_image(&p, 3, 4, 4, "The river at dawn, frost on the railings.");
    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_THREAD;
    CHECK(indigo_timeline_append(&app.thread, &p));
    CHECK(indigo_app_open_image(&app));

    /* The description is on the control screen, where there is room for the
     * whole sentence, and only when the setting asks for it. */
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(!text_has(&bottom, "frost"));
    CHECK(text_has(&bottom, "Close"));
    /* Four images and one of them shown is said out loud rather than left to
     * look like a post that happens to have one picture. */
    CHECK(text_has(&bottom, "1 of 4 images."));
    /* With the setting on, the description appears and nothing is pushed off
     * the bottom of the screen. */
    app.settings.alt_text = true;
    indigo_layout_build(&app, &in, &top, &bottom);
    /* Wrapped to the bottom screen's width, so it is words that are asserted
     * rather than the sentence: a line break in the middle of one would
     * otherwise read as the description having gone missing. */
    CHECK(text_has(&bottom, "frost"));
    CHECK(text_has(&bottom, "dawn"));
    CHECK(!bottom.overflow);
    for (unsigned i = 0; i < bottom.count; i++) {
        if (bottom.cmds[i].kind == INDIGO_CMD_TEXT) {
            CHECK(bottom.cmds[i].y < (float) INDIGO_BOTTOM_HEIGHT);
        }
    }

    /* No description is stated as such: an empty area reads as one that failed
     * to arrive, and most posts have no description at all. */
    with_image(&p, 3, 4, 1, "");
    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_THREAD;
    CHECK(indigo_timeline_append(&app.thread, &p));
    CHECK(indigo_app_open_image(&app));
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(text_has(&bottom, "no description"));
    CHECK(!text_has(&bottom, "1 of 1 images."));

    /* And a viewer with nothing in it says so rather than showing an empty
     * screen, which is what a stale state would look like. */
    app.image.url[0] = '\0';
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(text_has(&top, "No image to show."));
}

static void
test_image_viewer_is_entered_and_left_the_way_the_app_does(void)
{
    indigo_app app;
    indigo_input in = {0};
    indigo_rect image = indigo_layout_button_rect(INDIGO_ACTION_IMAGE);
    indigo_rect close = indigo_layout_button_rect(INDIGO_ACTION_BACK);
    indigo_post p = make_post("at://a/app.bsky.feed.post/1", "a river");

    with_image(&p, 3, 4, 1, "");
    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_THREAD;
    app.thread_focus = 0;
    CHECK(indigo_timeline_append(&app.thread, &p));

    /* Touch the button. */
    in.touch_pressed = true;
    in.touch_x = (int) (image.x + image.w / 2);
    in.touch_y = (int) (image.y + image.h / 2);
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_IMAGE);

    /* On the viewer, only Close is a control: a tap on the description does
     * nothing, so a thumb resting on the screen cannot close it by accident. */
    in.touch_x = (int) (close.x + close.w / 2);
    in.touch_y = (int) (close.y + close.h / 2);
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_THREAD);

    /* ZR is the same shortcut in both directions, which is what makes it a
     * path to the viewer rather than a separate feature. */
    in = (indigo_input) {0};
    in.zr = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_IMAGE);
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_THREAD);

    /* On a post with no picture, ZR does nothing at all rather than opening an
     * empty screen. */
    app.thread.posts[0].embed_thumb[0] = '\0';
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_THREAD);
}

static void
test_image_button_takes_the_status_line(void)
{
    indigo_app app;
    indigo_input in = {0};
    indigo_canvas top;
    indigo_canvas bottom;
    indigo_post p = make_post("at://a/app.bsky.feed.post/1", "a river");

    with_image(&p, 3, 4, 1, "");
    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_THREAD;
    CHECK(indigo_timeline_append(&app.thread, &p));

    /* With no status there is nothing to move, and the bar keeps the screen's
     * own title. */
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(count_text(&bottom, "Thread") == 1);

    /* A failure fetching the thread is worth reading, so it moves to the top
     * screen rather than being dropped for a button. */
    indigo_timeline_fail_fetch(&app.thread, "Could not reach the network.");
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(count_text(&bottom, "Could not reach") == 0);
    CHECK(text_has(&top, "Could not reach the network."));
    CHECK(!top.overflow && !bottom.overflow);
    /* The hint it stands in for is only lost while there is a status to show. */
    indigo_timeline_init(&app.thread);
    CHECK(indigo_timeline_append(&app.thread, &p));
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(!text_has(&top, "Could not reach"));
    CHECK(text_has(&top, "SEL  Profile"));
}

static void
test_media_forget_lets_the_viewer_decode_at_its_own_size(void)
{
    indigo_media_cache c;
    unsigned gen = 0;
    unsigned again = 0;
    int slot;
    int second;
    uint8_t *stale;

    indigo_media_init(&c);
    /* The detail band asked for a portrait photograph at the height it draws
     * it, which is 60px on the band and 240 on the viewer's screen. */
    CHECK(indigo_media_claim(&c, "https://cdn.example/river@jpeg", 60, &gen) >= 0);
    CHECK(indigo_media_publish(&c, 0, gen, fake_pixels(45, 60), 45, 60));
    CHECK(c.bytes == 45u * 60u * 4u);

    slot = indigo_media_slot_of(&c, "https://cdn.example/river@jpeg");
    CHECK(slot >= 0);
    CHECK(c.slots[slot].max_dim == 60);

    /* Dropping it is what lets the second claim be a different size. Without
     * that, first-request-wins hands the viewer the 60px copy, and a 60px
     * photograph blown up to a 400px screen is not a picture. */
    indigo_media_forget(&c, "https://cdn.example/river@jpeg");
    CHECK(c.bytes == 0);
    CHECK(indigo_media_slot_of(&c, "https://cdn.example/river@jpeg") == -1);
    CHECK(!indigo_media_known(&c, "https://cdn.example/river@jpeg"));

    second = indigo_media_claim(&c, "https://cdn.example/river@jpeg", INDIGO_TOP_WIDTH, &again);
    CHECK(second >= 0);
    CHECK(again != gen);
    if (second >= 0) {
        CHECK(c.slots[second].max_dim == INDIGO_TOP_WIDTH);
    }

    /* Forgetting a URL the cache never had is a no-op, and so is forgetting
     * one twice. */
    indigo_media_forget(&c, "https://cdn.example/never@jpeg");
    CHECK(c.bytes == 0);
    indigo_media_forget(&c, "https://cdn.example/river@jpeg");
    CHECK(indigo_media_slot_of(&c, "https://cdn.example/river@jpeg") == -1);

    /* A fetch already in flight when the slot is dropped is not cancelled; its
     * result arrives with a stale generation and is dropped by publish, which
     * is what keeps a forgotten image from coming back at the old size. */
    indigo_media_init(&c);
    CHECK(indigo_media_claim(&c, "https://cdn.example/busy@jpeg", 60, &gen) >= 0);
    indigo_media_forget(&c, "https://cdn.example/busy@jpeg");
    stale = fake_pixels(45, 60);
    CHECK(!indigo_media_publish(&c, 0, gen, stale, 45, 60));
    free(stale); /* publish refused it, so it is still the caller's */
    CHECK(c.bytes == 0);
    CHECK(c.slots[0].state == INDIGO_MEDIA_EMPTY);
}

/* ---- Self-update: release identity, URL prefix, journal, swap ----------- */

static void
test_update_release_of(void)
{
    char v[INDIGO_UPDATE_VERSION_MAX];
    bool dev = true;

    CHECK(indigo_update_release_of("v0.5.0", v, sizeof v, &dev) && !strcmp(v, "0.5.0") && !dev);
    CHECK(indigo_update_release_of("0.10.2", v, sizeof v, &dev) && !strcmp(v, "0.10.2") && !dev);
    CHECK(indigo_update_release_of("v0.5.0-3-gabc1234", v, sizeof v, &dev) && !strcmp(v, "0.5.0") &&
          dev);
    CHECK(indigo_update_release_of("v0.5.0-dirty", v, sizeof v, &dev) && dev);
    CHECK(indigo_update_release_of("v0.5.0-12-g0123abcd-dirty", v, sizeof v, &dev) && dev);
    /* What describe --always gives with no tag, and other things that are not
     * a release. */
    CHECK(!indigo_update_release_of("abc1234", v, sizeof v, &dev) && v[0] == '\0');
    CHECK(!indigo_update_release_of("unknown", v, sizeof v, &dev));
    CHECK(!indigo_update_release_of("", v, sizeof v, &dev));
    CHECK(!indigo_update_release_of("v0.5", v, sizeof v, &dev));
    CHECK(!indigo_update_release_of("v0.5.0.1", v, sizeof v, &dev));
    CHECK(!indigo_update_release_of("v01.5.0", v, sizeof v, &dev));
    CHECK(!indigo_update_release_of("v0.5.0-rc.1", v, sizeof v, &dev));
    CHECK(!indigo_update_release_of("v0.5.0-3-gXYZ", v, sizeof v, &dev));
    CHECK(!indigo_update_release_of("v0.5.0-3", v, sizeof v, &dev));
    CHECK(!indigo_update_release_of("v0.5.0 ", v, sizeof v, &dev));
    CHECK(!indigo_update_release_of("v99999.0.0", v, sizeof v, &dev));
    CHECK(!indigo_update_release_of("v0.5.0", v, 5, &dev));
    CHECK(!indigo_update_release_of(NULL, v, sizeof v, &dev));
}

static void
test_update_asset_prefix(void)
{
    char url[INDIGO_UPDATE_URL_MAX];

    CHECK(indigo_update_asset_prefix("0.6.0", url, sizeof url));
    CHECK(!strcmp(url, "https://github.com/ewanc26/indigo/releases/download/v0.6.0/"));
    /* Only a plain release number: nothing that could add path segments. */
    CHECK(!indigo_update_asset_prefix("v0.6.0", url, sizeof url));
    CHECK(!indigo_update_asset_prefix("0.6.0-1-gabcd", url, sizeof url));
    CHECK(!indigo_update_asset_prefix("0.6.0/../../evil", url, sizeof url));
    CHECK(!indigo_update_asset_prefix("", url, sizeof url));
    CHECK(!indigo_update_asset_prefix("0.6.0", url, 20));
    CHECK(!strcmp(INDIGO_UPDATE_MANIFEST_URL,
                  "https://github.com/ewanc26/indigo/releases/latest/download/update.json"));
}

static void
test_update_paths(void)
{
    indigo_update_paths p;

    CHECK(indigo_update_paths_from("sdmc:/3ds/indigo.3dsx", "sdmc:/3ds/indigo/update.state", &p));
    CHECK(!strcmp(p.target, "sdmc:/3ds/indigo.3dsx"));
    CHECK(!strcmp(p.staged, "sdmc:/3ds/indigo.3dsx.new"));
    CHECK(!strcmp(p.backup, "sdmc:/3ds/indigo-previous.3dsx"));
    CHECK(!strcmp(p.state, "sdmc:/3ds/indigo/update.state"));
    CHECK(indigo_update_paths_from("sdmc:/3ds/indigo/indigo.3dsx", "s", &p));
    CHECK(!strcmp(p.backup, "sdmc:/3ds/indigo/indigo-previous.3dsx"));
    /* Not from the SD card, not a .3dsx, or no argv at all: no self-update. */
    CHECK(!indigo_update_paths_from("3dslink:/indigo.3dsx", "s", &p));
    CHECK(!indigo_update_paths_from("sdmc:/3ds/indigo.cia", "s", &p));
    CHECK(!indigo_update_paths_from("", "s", &p));
    CHECK(!indigo_update_paths_from(NULL, "s", &p));
    CHECK(!indigo_update_paths_from("sdmc:/.3dsx", "s", &p));
    CHECK(!indigo_update_paths_from("sdmc:/3ds/../x/indigo.3dsx", "s", &p));
}

static void
test_update_state_codec(void)
{
    indigo_update_state s;
    indigo_update_state back;
    char buf[256];
    size_t len = 0;

    memset(&s, 0, sizeof s);
    s.phase = INDIGO_UPDATE_PHASE_SWAPPING;
    strcpy(s.version, "0.6.0");
    for (int i = 0; i < 32; i++) {
        s.sha256[i] = (unsigned char) (i * 7);
    }
    CHECK(indigo_update_state_encode(&s, buf, sizeof buf, &len) == INDIGO_CODEC_OK);
    CHECK(indigo_update_state_decode(buf, len, &back) == INDIGO_CODEC_OK);
    CHECK(back.phase == s.phase && !strcmp(back.version, "0.6.0") &&
          !memcmp(back.sha256, s.sha256, 32));
    /* Every truncation is detected rather than loaded. */
    for (size_t cut = 0; cut < len; cut++) {
        CHECK(indigo_update_state_decode(buf, cut, &back) != INDIGO_CODEC_OK);
    }
    CHECK(indigo_update_state_decode("", 0, &back) == INDIGO_CODEC_EMPTY);
    {
        static const char bad_phase[] = "indigo-update-state 1\nphase=done\nversion=0.6.0\nsha256="
                                        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\nend\n";
        static const char upper[] = "indigo-update-state 1\nphase=staged\nversion=0.6.0\nsha256="
                                    "000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F\nend\n";
        static const char dup[] = "indigo-update-state 1\nphase=staged\nphase=staged\nversion=0.6.0\nsha256="
                                  "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\nend\n";
        static const char devver[] = "indigo-update-state 1\nphase=staged\nversion=0.6.0-1-gabcd\nsha256="
                                     "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\nend\n";
        CHECK(indigo_update_state_decode(bad_phase, sizeof bad_phase - 1, &back) == INDIGO_CODEC_CORRUPT);
        CHECK(indigo_update_state_decode(upper, sizeof upper - 1, &back) == INDIGO_CODEC_CORRUPT);
        CHECK(indigo_update_state_decode(dup, sizeof dup - 1, &back) == INDIGO_CODEC_CORRUPT);
        CHECK(indigo_update_state_decode(devver, sizeof devver - 1, &back) == INDIGO_CODEC_CORRUPT);
    }
    s.phase = INDIGO_UPDATE_PHASE_NONE;
    CHECK(indigo_update_state_encode(&s, buf, sizeof buf, &len) == INDIGO_CODEC_CORRUPT);
    s.phase = INDIGO_UPDATE_PHASE_STAGED;
    CHECK(indigo_update_state_encode(&s, buf, 20, &len) == INDIGO_CODEC_TOO_BIG);
}

/* A fake SD card. Files hold a short label ("old", "new", "bad"); a file's
 * "hash" is its label zero-padded to 32 bytes, which is all the swap needs to
 * tell builds apart. `budget` is how many more writes succeed before the
 * console "loses power": after that every write fails and changes nothing. */
#define FAKE_FILES 8
typedef struct {
    char name[INDIGO_UPDATE_PATH_MAX];
    char data[256];
    size_t len;
    bool used;
} fake_file;

typedef struct {
    fake_file files[FAKE_FILES];
    int budget; /* -1 = unlimited */
    int writes;
} fake_sd;

static fake_file *
fake_find(fake_sd *sd, const char *name)
{
    for (int i = 0; i < FAKE_FILES; i++) {
        if (sd->files[i].used && !strcmp(sd->files[i].name, name)) {
            return &sd->files[i];
        }
    }
    return NULL;
}

static bool
fake_spend(fake_sd *sd)
{
    if (sd->budget == 0) {
        return false;
    }
    if (sd->budget > 0) {
        sd->budget--;
    }
    sd->writes++;
    return true;
}

static void
fake_put(fake_sd *sd, const char *name, const char *data, size_t len)
{
    fake_file *f = fake_find(sd, name);
    for (int i = 0; !f && i < FAKE_FILES; i++) {
        if (!sd->files[i].used) {
            f = &sd->files[i];
        }
    }
    if (!f) {
        return;
    }
    f->used = true;
    snprintf(f->name, sizeof f->name, "%s", name);
    memcpy(f->data, data, len);
    f->len = len;
}

static const char *
fake_label(fake_sd *sd, const char *name)
{
    fake_file *f = fake_find(sd, name);
    static char out[256];
    if (!f) {
        return NULL;
    }
    memcpy(out, f->data, f->len);
    out[f->len] = '\0';
    return out;
}

static bool
fake_exists(void *ctx, const char *path)
{
    return fake_find(ctx, path) != NULL;
}

static bool
fake_rename(void *ctx, const char *from, const char *to)
{
    fake_sd *sd = ctx;
    fake_file *f = fake_find(sd, from);
    if (!f || fake_find(sd, to) || !fake_spend(sd)) {
        return false; /* FAT rename does not overwrite */
    }
    snprintf(f->name, sizeof f->name, "%s", to);
    return true;
}

static bool
fake_remove(void *ctx, const char *path)
{
    fake_sd *sd = ctx;
    fake_file *f = fake_find(sd, path);
    if (!f || !fake_spend(sd)) {
        return false;
    }
    f->used = false;
    return true;
}

/* The 3DS copy writes a temporary and renames it, so it is all or nothing. */
static bool
fake_copy(void *ctx, const char *from, const char *to)
{
    fake_sd *sd = ctx;
    fake_file *f = fake_find(sd, from);
    if (!f || fake_find(sd, to) || !fake_spend(sd)) {
        return false;
    }
    fake_put(sd, to, f->data, f->len);
    return true;
}

static void
fake_digest(const char *label, size_t len, unsigned char out[32])
{
    memset(out, 0, 32);
    memcpy(out, label, len < 32 ? len : 32);
}

static bool
fake_sha256(void *ctx, const char *path, unsigned char out[32])
{
    fake_file *f = fake_find(ctx, path);
    if (!f) {
        return false;
    }
    fake_digest(f->data, f->len, out);
    return true;
}

static bool
fake_write_state(void *ctx, const char *path, const indigo_update_state *s)
{
    fake_sd *sd = ctx;
    char buf[256];
    size_t len = 0;
    if (indigo_update_state_encode(s, buf, sizeof buf, &len) != INDIGO_CODEC_OK || !fake_spend(sd)) {
        return false;
    }
    fake_put(sd, path, buf, len);
    return true;
}

static bool
fake_read_state(void *ctx, const char *path, indigo_update_state *s)
{
    fake_file *f = fake_find(ctx, path);
    return f && indigo_update_state_decode(f->data, f->len, s) == INDIGO_CODEC_OK;
}

static indigo_update_fs
fake_fs(fake_sd *sd)
{
    indigo_update_fs fs = {sd,          fake_exists, fake_rename,      fake_remove,
                           fake_copy,   fake_sha256, fake_write_state, fake_read_state};
    return fs;
}

static void
fake_setup(fake_sd *sd, indigo_update_paths *p, const char *staged_label)
{
    memset(sd, 0, sizeof *sd);
    sd->budget = -1;
    CHECK(indigo_update_paths_from("sdmc:/3ds/indigo.3dsx", "sdmc:/3ds/indigo/update.state", p));
    fake_put(sd, p->target, "old", 3);
    fake_put(sd, p->staged, staged_label, strlen(staged_label));
}

/* Which build the person can launch, the way the Homebrew Menu would find it:
 * the target if it is there, otherwise the backup. NULL means nothing. */
static const char *
fake_boot(fake_sd *sd, const indigo_update_paths *p, const char **path, const char **version)
{
    const char *label = fake_label(sd, p->target);
    *path = p->target;
    if (!label) {
        label = fake_label(sd, p->backup);
        *path = p->backup;
    }
    if (!label) {
        return NULL;
    }
    *version = !strcmp(label, "new") ? "0.6.0" : "0.5.0";
    return label;
}

static void
test_update_stage_verifies_from_disk(void)
{
    fake_sd sd;
    indigo_update_paths p;
    indigo_update_fs fs;
    unsigned char want[32];

    fake_setup(&sd, &p, "bad");
    fs = fake_fs(&sd);
    fake_digest("new", 3, want);
    /* The bytes on the card are not the release: refused and removed. */
    CHECK(indigo_update_stage(&fs, &p, "0.6.0", want) == INDIGO_UPDATE_NOT_STAGED);
    CHECK(!fake_exists(&sd, p.staged));
    CHECK(!fake_exists(&sd, p.state));
    CHECK(indigo_update_install(&fs, &p) == INDIGO_UPDATE_NOT_STAGED);
    CHECK(!strcmp(fake_label(&sd, p.target), "old"));
    /* A dev version is never staged. */
    fake_put(&sd, p.staged, "new", 3);
    CHECK(indigo_update_stage(&fs, &p, "0.6.0-1-gabcd", want) == INDIGO_UPDATE_NOT_STAGED);
}

static void
test_update_install_happy_path(void)
{
    fake_sd sd;
    indigo_update_paths p;
    indigo_update_fs fs;
    unsigned char want[32];

    fake_setup(&sd, &p, "new");
    fs = fake_fs(&sd);
    fake_digest("new", 3, want);
    CHECK(indigo_update_stage(&fs, &p, "0.6.0", want) == INDIGO_UPDATE_OK);
    CHECK(indigo_update_install(&fs, &p) == INDIGO_UPDATE_OK);
    CHECK(!strcmp(fake_label(&sd, p.target), "new"));
    CHECK(!strcmp(fake_label(&sd, p.backup), "old"));
    CHECK(!fake_exists(&sd, p.staged));
    /* The old build is launched from its backup: it waits, keeps everything. */
    CHECK(indigo_update_recover(&fs, &p, "0.5.0", p.backup) == INDIGO_RECOVER_WAITING);
    CHECK(fake_exists(&sd, p.backup));
    /* The new build boots from the target: the backup goes. */
    CHECK(indigo_update_recover(&fs, &p, "0.6.0", p.target) == INDIGO_RECOVER_CONFIRMED);
    CHECK(!fake_exists(&sd, p.backup) && !fake_exists(&sd, p.state));
    CHECK(indigo_update_recover(&fs, &p, "0.6.0", p.target) == INDIGO_RECOVER_NOTHING);
}

/* Pull the power after every possible number of writes, in the install and
 * then in each recovery after it, and require that (a) there is always a
 * build to launch, (b) only "old" or the verified "new" is ever at the target,
 * and (c) a few clean boots later the card is tidy. */
static void
test_update_survives_power_loss_anywhere(void)
{
    for (int k = 0; k < 8; k++) {
        for (int r = 0; r < 4; r++) {
            fake_sd sd;
            indigo_update_paths p;
            indigo_update_fs fs;
            unsigned char want[32];
            const char *path;
            const char *version = NULL;
            const char *label;
            bool installed;

            fake_setup(&sd, &p, "new");
            fs = fake_fs(&sd);
            fake_digest("new", 3, want);
            CHECK(indigo_update_stage(&fs, &p, "0.6.0", want) == INDIGO_UPDATE_OK);
            sd.budget = k;
            installed = indigo_update_install(&fs, &p) == INDIGO_UPDATE_OK;
            CHECK(installed == (k >= 4));

            for (int boot = 0; boot < 6; boot++) {
                label = fake_boot(&sd, &p, &path, &version);
                CHECK(label != NULL);
                if (!label) {
                    break;
                }
                CHECK(!strcmp(label, "old") || !strcmp(label, "new"));
                /* The first recovery is itself interrupted after r writes. */
                sd.budget = boot == 0 ? r : -1;
                indigo_update_recover(&fs, &p, version, path);
            }
            label = fake_boot(&sd, &p, &path, &version);
            CHECK(label && !strcmp(path, p.target));
            CHECK(!fake_exists(&sd, p.staged));
            CHECK(!fake_exists(&sd, p.state));
            CHECK(!fake_exists(&sd, p.backup));
            if (installed) {
                CHECK(label && !strcmp(label, "new"));
            }
        }
    }
}

static void
test_update_recover_rejects_a_damaged_staged_file(void)
{
    fake_sd sd;
    indigo_update_paths p;
    indigo_update_fs fs;
    indigo_update_state s;

    /* Journal says swapping, the target has moved, and the staged file is not
     * the release the journal names: the old build goes back. */
    fake_setup(&sd, &p, "bad");
    fs = fake_fs(&sd);
    memset(&s, 0, sizeof s);
    s.phase = INDIGO_UPDATE_PHASE_SWAPPING;
    strcpy(s.version, "0.6.0");
    fake_digest("new", 3, s.sha256);
    CHECK(fake_write_state(&sd, p.state, &s));
    CHECK(fake_rename(&sd, p.target, p.backup));
    /* Launched from the backup: it is copied, never moved out from under the
     * running build. */
    CHECK(indigo_update_recover(&fs, &p, "0.5.0", p.backup) == INDIGO_RECOVER_RESTORED_BACKUP);
    CHECK(!strcmp(fake_label(&sd, p.target), "old"));
    CHECK(fake_exists(&sd, p.backup));
    CHECK(!fake_exists(&sd, p.staged) && !fake_exists(&sd, p.state));
}

static void
test_update_recover_without_a_journal(void)
{
    fake_sd sd;
    indigo_update_paths p;
    indigo_update_fs fs;

    /* A stray staged file and a damaged journal: the file goes, the target and
     * any backup are left alone. */
    fake_setup(&sd, &p, "new");
    fs = fake_fs(&sd);
    fake_put(&sd, p.state, "garbage", 7);
    fake_put(&sd, p.backup, "old", 3);
    CHECK(indigo_update_recover(&fs, &p, "0.5.0", p.target) == INDIGO_RECOVER_DISCARDED_STAGED);
    CHECK(!fake_exists(&sd, p.staged) && !fake_exists(&sd, p.state));
    CHECK(fake_exists(&sd, p.target) && fake_exists(&sd, p.backup));
    CHECK(indigo_update_recover(&fs, &p, NULL, p.target) == INDIGO_RECOVER_NOTHING);
}

/* ---- The update screen -------------------------------------------------- */

static void
test_update_asset_ok(void)
{
    const char *good_url = "https://github.com/ewanc26/indigo/releases/download/v0.7.0/indigo-0.7.0.3dsx";

    CHECK(indigo_update_asset_ok("0.7.0", "indigo-0.7.0.3dsx", good_url));
    /* A different file name, a different release's URL, a different host, and
     * a query string tacked on: each is refused. */
    CHECK(!indigo_update_asset_ok("0.7.0", "indigo.3dsx", good_url));
    CHECK(!indigo_update_asset_ok("0.7.0", "indigo-0.7.0.3dsx",
                                  "https://github.com/ewanc26/indigo/releases/download/v0.6.0/indigo-0.7.0.3dsx"));
    CHECK(!indigo_update_asset_ok("0.7.0", "indigo-0.7.0.3dsx",
                                  "https://example.com/ewanc26/indigo/releases/download/v0.7.0/indigo-0.7.0.3dsx"));
    CHECK(!indigo_update_asset_ok("0.7.0", "indigo-0.7.0.3dsx",
                                  "https://github.com/ewanc26/indigo/releases/download/v0.7.0/indigo-0.7.0.3dsx?x=1"));
    CHECK(!indigo_update_asset_ok("0.7.0", "indigo-0.7.0.3dsx",
                                  "http://github.com/ewanc26/indigo/releases/download/v0.7.0/indigo-0.7.0.3dsx"));
    CHECK(!indigo_update_asset_ok("0.7.0-rc.1", "indigo-0.7.0-rc.1.3dsx", good_url));
    CHECK(!indigo_update_asset_ok(NULL, "indigo-0.7.0.3dsx", good_url));
    CHECK(!indigo_update_asset_ok("0.7.0", NULL, good_url));
}

/* A throwaway key made for this test: its private half was deleted after it signed the
 * manifest below, so no key material is in the repository. Indigo's real key is never
 * used to sign anything here. */
#define TEST_PUBKEY_HEX "5e6d1c7414f43fa333140dd15fb370a945157e50be2c49840006aa2aa0e93785"
#define TEST_MANIFEST \
    "{\"schema\":1,\"app\":\"indigo\",\"version\":\"9.9.9\",\"notes\":\"n\",\"asset\":{\"name\":\"indigo-9.9.9.3dsx\",\"url\":\"https://github.com/ewanc26/indigo/releases/download/v9.9.9/indigo-9.9.9.3dsx\",\"size\":3,\"sha256\":\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\"},\"signature\":null}"
#define TEST_SIG \
    "25874955062ebfe1cf8a79e2724ba3e0f15f7ac84e010ad33fd702804885d64c" \
    "fb85677a41e0921d5418892668529e5869d1abcbecbc6d3aef9b54a9040f280b"

static void
test_update_signature_gate(void)
{
    unsigned char test_pk[INDIGO_UPDATE_PUBLIC_KEY_LEN];
    unsigned char real_pk[INDIGO_UPDATE_PUBLIC_KEY_LEN];
    const char *test_hex = TEST_PUBKEY_HEX;
    char tampered[sizeof TEST_MANIFEST];
    char sig[sizeof TEST_SIG + 1];
    const char *m = TEST_MANIFEST;
    const size_t mlen = strlen(m);
    size_t i;

    for (i = 0; i < sizeof test_pk; i++) {
        unsigned v;
        CHECK(sscanf(&test_hex[2 * i], "%2x", &v) == 1);
        test_pk[i] = (unsigned char) v;
    }
    CHECK(indigo_update_public_key(real_pk));
    CHECK(memcmp(real_pk, test_pk, sizeof real_pk) != 0);

    CHECK(indigo_update_verify_manifest(m, mlen, TEST_SIG, strlen(TEST_SIG), test_pk));
    /* A signature file as a shell redirect leaves it, with its newline. */
    snprintf(sig, sizeof sig, "%s\n", TEST_SIG);
    CHECK(indigo_update_verify_manifest(m, mlen, sig, strlen(sig), test_pk));

    /* Refused: no signature, a short one, one changed digit, one changed byte of the
     * manifest, and the right signature under Indigo's real key (which did not sign it). */
    CHECK(!indigo_update_verify_manifest(m, mlen, NULL, 0, test_pk));
    CHECK(!indigo_update_verify_manifest(m, mlen, "", 0, test_pk));
    CHECK(!indigo_update_verify_manifest(m, mlen, TEST_SIG, strlen(TEST_SIG) - 2, test_pk));
    snprintf(sig, sizeof sig, "%s", TEST_SIG);
    sig[7] = sig[7] == '0' ? '1' : '0';
    CHECK(!indigo_update_verify_manifest(m, mlen, sig, strlen(sig), test_pk));
    snprintf(tampered, sizeof tampered, "%s", m);
    tampered[mlen - 10] = 'x';
    CHECK(!indigo_update_verify_manifest(tampered, mlen, TEST_SIG, strlen(TEST_SIG), test_pk));
    CHECK(!indigo_update_verify_manifest(m, mlen, TEST_SIG, strlen(TEST_SIG), real_pk));
    CHECK(!indigo_update_verify_manifest(NULL, 0, TEST_SIG, strlen(TEST_SIG), test_pk));

    /* The pinned signature URL sits beside the manifest's. */
    CHECK(strcmp(INDIGO_UPDATE_SIGNATURE_URL,
                 "https://github.com/ewanc26/indigo/releases/latest/download/update.json.sig") == 0);
}

static void
test_cdn_url(void)
{
    /* The 130-byte avatar URL the AppView returns: longer than the 128 bytes the
     * URL buffers used to be, which cut off the blob key and made the CDN answer 400. */
    const char *avatar =
        "https://cdn.bsky.app/img/avatar/plain/did:plc:z72i7hdynmk6r22z27h6tvur/"
        "bafkreihwihm6kpd6zuwhhlro75p5qks5qtrcu55jp3gddbfjsieiv7wuka";
    const char *thumb =
        "https://cdn.bsky.app/img/feed_thumbnail/plain/did:plc:z72i7hdynmk6r22z27h6tvur/"
        "bafkreihwihm6kpd6zuwhhlro75p5qks5qtrcu55jp3gddbfjsieiv7wuka@jpeg";
    char out[INDIGO_MEDIA_URL_MAX];
    char tiny[16];

    CHECK(strlen(avatar) > 128);
    indigo_media_cdn_url(out, sizeof out, avatar, INDIGO_CDN_AVATAR);
    CHECK(strcmp(out,
                 "https://cdn.bsky.app/img/avatar_thumbnail/plain/did:plc:z72i7hdynmk6r22z27h6tvur/"
                 "bafkreihwihm6kpd6zuwhhlro75p5qks5qtrcu55jp3gddbfjsieiv7wuka@jpeg") == 0);
    CHECK(strlen(out) < sizeof out);
    indigo_media_cdn_url(out, sizeof out, avatar, INDIGO_CDN_THUMBNAIL);
    CHECK(strstr(out, "/img/feed_thumbnail/plain/") != NULL && strstr(out, "@jpeg") != NULL);
    indigo_media_cdn_url(out, sizeof out, thumb, INDIGO_CDN_THUMBNAIL);
    CHECK(strcmp(out, thumb) == 0);

    /* Not a Bluesky CDN URL: unchanged. Empty and NULL: empty. A result that does not fit:
     * empty, never a truncated URL. */
    indigo_media_cdn_url(out, sizeof out, "https://example.com/a.png", INDIGO_CDN_AVATAR);
    CHECK(strcmp(out, "https://example.com/a.png") == 0);
    indigo_media_cdn_url(out, sizeof out, "", INDIGO_CDN_AVATAR);
    CHECK(out[0] == '\0');
    indigo_media_cdn_url(out, sizeof out, NULL, INDIGO_CDN_AVATAR);
    CHECK(out[0] == '\0');
    indigo_media_cdn_url(tiny, sizeof tiny, avatar, INDIGO_CDN_AVATAR);
    CHECK(tiny[0] == '\0');
    indigo_media_cdn_url(tiny, sizeof tiny, "https://example.com/this-is-too-long-to-fit", INDIGO_CDN_AVATAR);
    CHECK(tiny[0] == '\0');
}

static void
test_updater_eligibility(void)
{
    indigo_updater u;

    indigo_updater_init(&u, "v0.6.0", true);
    CHECK(u.state == INDIGO_UPDATER_IDLE && !strcmp(u.current, "0.6.0"));
    /* Not launched from a .3dsx on the card, past its tag, dirty, or with no
     * release to name: never offered an update, with a reason. */
    indigo_updater_init(&u, "v0.6.0", false);
    CHECK(u.state == INDIGO_UPDATER_UNAVAILABLE && u.message[0]);
    indigo_updater_init(&u, "v0.6.0-3-gabc1234", true);
    CHECK(u.state == INDIGO_UPDATER_UNAVAILABLE && u.message[0]);
    indigo_updater_init(&u, "v0.6.0-dirty", true);
    CHECK(u.state == INDIGO_UPDATER_UNAVAILABLE);
    indigo_updater_init(&u, "abc1234", true);
    CHECK(u.state == INDIGO_UPDATER_UNAVAILABLE && u.current[0] == '\0');
    indigo_updater_init(&u, NULL, true);
    CHECK(u.state == INDIGO_UPDATER_UNAVAILABLE);
    CHECK(indigo_updater_action_for(&u) == INDIGO_UPDATER_ACT_NONE);
}

static void
test_updater_transitions(void)
{
    indigo_updater u;
    char label[48];

    indigo_updater_init(&u, "v0.6.0", true);
    CHECK(indigo_updater_action_for(&u) == INDIGO_UPDATER_ACT_CHECK);
    CHECK(!strcmp(indigo_updater_button_label(&u, label, sizeof label), "Check for updates"));

    /* An install cannot be started before an update has been shown. */
    indigo_updater_begin_install(&u);
    CHECK(u.state == INDIGO_UPDATER_IDLE);
    indigo_updater_staged(&u);
    indigo_updater_installed(&u);
    CHECK(u.state == INDIGO_UPDATER_IDLE);

    indigo_updater_begin_check(&u);
    CHECK(u.state == INDIGO_UPDATER_CHECKING);
    CHECK(indigo_updater_action_for(&u) == INDIGO_UPDATER_ACT_NONE);
    CHECK(indigo_updater_button_label(&u, label, sizeof label)[0] == '\0');
    indigo_updater_check_done(&u, "0.6.0", 100, false);
    CHECK(u.state == INDIGO_UPDATER_UP_TO_DATE);

    indigo_updater_begin_check(&u);
    indigo_updater_check_done(&u, "0.7.0", 1442404, true);
    CHECK(u.state == INDIGO_UPDATER_AVAILABLE && !strcmp(u.latest, "0.7.0") && u.size == 1442404);
    CHECK(indigo_updater_action_for(&u) == INDIGO_UPDATER_ACT_INSTALL);
    CHECK(!strcmp(indigo_updater_button_label(&u, label, sizeof label), "Install 0.7.0"));

    indigo_updater_begin_install(&u);
    CHECK(u.state == INDIGO_UPDATER_DOWNLOADING);
    CHECK(indigo_updater_action_for(&u) == INDIGO_UPDATER_ACT_NONE);
    /* A late "checked" from an earlier request cannot move it. */
    indigo_updater_check_done(&u, "0.9.9", 1, true);
    CHECK(u.state == INDIGO_UPDATER_DOWNLOADING && !strcmp(u.latest, "0.7.0"));
    indigo_updater_staged(&u);
    CHECK(u.state == INDIGO_UPDATER_READY);
    indigo_updater_installed(&u);
    CHECK(u.state == INDIGO_UPDATER_INSTALLED);
    /* Once swapped, nothing can reopen the question. */
    indigo_updater_fail(&u, "late failure");
    indigo_updater_begin_check(&u);
    CHECK(u.state == INDIGO_UPDATER_INSTALLED);

    /* A failure keeps the message, offers a retry, and a retry clears it. */
    indigo_updater_init(&u, "v0.6.0", true);
    indigo_updater_begin_check(&u);
    indigo_updater_fail(&u, "Could not reach GitHub.");
    CHECK(u.state == INDIGO_UPDATER_FAILED && !strcmp(u.message, "Could not reach GitHub."));
    CHECK(!strcmp(indigo_updater_button_label(&u, label, sizeof label), "Try again"));
    indigo_updater_begin_check(&u);
    CHECK(u.state == INDIGO_UPDATER_CHECKING && u.message[0] == '\0');

    /* An unavailable build stays unavailable whatever it is told. */
    indigo_updater_init(&u, "v0.6.0", false);
    indigo_updater_begin_check(&u);
    indigo_updater_fail(&u, "x");
    CHECK(u.state == INDIGO_UPDATER_UNAVAILABLE);
}

static void
test_update_screen_asks_before_it_installs(void)
{
    indigo_app app;
    indigo_input in = {0};
    indigo_canvas top;
    indigo_canvas bottom;

    indigo_app_init(&app);
    indigo_app_set_updater(&app, "v0.6.0", true);
    indigo_app_open_update(&app);
    CHECK(app.screen == INDIGO_SCREEN_UPDATE);

    /* Opening the screen asks for nothing; A on it asks to check. */
    CHECK(indigo_app_peek_request(&app) == INDIGO_REQUEST_NONE);
    in.confirm = true;
    indigo_app_update(&app, &in);
    CHECK(indigo_app_take_request(&app, NULL) == INDIGO_REQUEST_UPDATE_CHECK);
    CHECK(app.updater.state == INDIGO_UPDATER_CHECKING);
    /* While it is checking, A does nothing. */
    indigo_app_update(&app, &in);
    CHECK(indigo_app_peek_request(&app) == INDIGO_REQUEST_NONE);

    indigo_updater_check_done(&app.updater, "0.7.0", 1000, true);
    /* The version is shown first: nothing has been requested yet. */
    CHECK(indigo_app_peek_request(&app) == INDIGO_REQUEST_NONE);
    indigo_layout_build(&app, &in, &top, &bottom);
    CHECK(bottom.count > 0);
    in.confirm = true;
    indigo_app_update(&app, &in);
    CHECK(indigo_app_take_request(&app, NULL) == INDIGO_REQUEST_UPDATE_INSTALL);
    CHECK(app.updater.state == INDIGO_UPDATER_DOWNLOADING);

    /* The button is touchable on its own rectangle, and B goes back. */
    indigo_app_init(&app);
    indigo_app_set_updater(&app, "v0.6.0", true);
    indigo_app_open_update(&app);
    {
        indigo_rect r = indigo_layout_button_rect(INDIGO_ACTION_UPDATE);
        indigo_input touch = {0};

        touch.touch_pressed = true;
        touch.touch_x = (int) (r.x + r.w / 2);
        touch.touch_y = (int) (r.y + r.h / 2);
        indigo_app_update(&app, &touch);
        CHECK(indigo_app_take_request(&app, NULL) == INDIGO_REQUEST_UPDATE_CHECK);
    }
    {
        indigo_input back = {0};

        back.back = true;
        indigo_app_update(&app, &back);
        CHECK(app.screen != INDIGO_SCREEN_UPDATE);
    }

    /* A development build offers no button at all. */
    indigo_app_init(&app);
    indigo_app_set_updater(&app, "v0.6.0-2-gabc1234", true);
    indigo_app_open_update(&app);
    in.confirm = true;
    indigo_app_update(&app, &in);
    CHECK(indigo_app_peek_request(&app) == INDIGO_REQUEST_NONE);
}

int
main(void)
{
    test_canvas_basics();
    test_canvas_overflow_is_bounded();
    test_app_navigation();
    test_touch_navigation();
    test_drag_scrolls_feeds();
    test_buttons_spaced_and_on_screen();
    test_layout_invariants();
    test_thread_navigation();
    test_compose_reply_gate();
    test_new_post_reply_gate();
    test_compose_gate_text_fits();
    test_attach_image();
    test_store_file();
    test_compose_flow();
    test_notifications();
    test_normalise_service();
    test_normalise_handle();
    test_signin_fields();
    test_signin_flow();
    test_signin_targets_spaced();
    test_session_codec();
    test_session_store();
    test_settings_defaults_and_clamp();
    test_settings_codec();
    test_settings_store();
    test_draft_store();
    test_settings_reach_the_app();
    test_failures();
    test_log_file();
    test_autofill();
    test_timeline_bounds();
    test_timeline_selection();
    test_timeline_paging();
    test_timeline_actions();
    test_copy_utf8();
    test_wrap();
    test_canvas_spans();
    test_home_requests();
    test_facet_menu();
    test_facet_menu_edges();
    test_liked_by_from_the_menu();
    test_menu_rows_on_screen();
    test_search_model();
    test_search_selection_scroll();
    test_search_flow();
    test_search_query_resets_results();
    test_search_empty_and_failure();
    test_search_results_bounded();
    test_search_paging();
    test_no_duplicate_back_hints();
    test_follow_toggle();
    test_follow_failure_reverts();
    test_follow_guards();
    test_mute_block_toggle();
    test_mute_block_failure_reverts();
    test_block_without_uri();
    test_graph_guards();
    test_people_lists();
    test_post_search();
    test_author_posts();
    test_pinned_post();
    test_lists();
    test_feeds();
    test_prefs();
    test_text_stays_on_screen();
    test_settings_screen();
    test_canvas_image_dedupes_by_url();
    test_canvas_image_refuses_unusable_urls();
    test_canvas_image_entry_cap();
    test_media_claim_and_publish();
    test_media_rejects_stale_result();
    test_media_failed_url_is_not_refetched();
    test_media_eviction_prefers_least_recently_used();
    test_media_never_evicts_in_flight();
    test_media_byte_budget();
    test_media_claim_guards();
    test_media_placeholder_colour();
    test_layout_draws_avatars();
    test_shapes_stay_on_screen();
    test_settings_screen_shows_the_build();
    test_layout_draws_post_images();
    test_layout_draws_video_poster();
    test_layout_draws_link_cards();
    test_layout_draws_alt_text();
    test_layout_text_scale();
    test_image_viewer_opens_from_the_selected_post();
    test_image_viewer_needs_an_image();
    test_image_viewer_holds_a_copy();
    test_image_viewer_from_search_results();
    test_image_button_is_offered_only_when_there_is_an_image();
    test_image_button_keeps_the_header_bar_readable();
    test_image_viewer_draws_the_picture_at_the_screen();
    test_image_viewer_says_what_the_picture_is();
    test_image_viewer_is_entered_and_left_the_way_the_app_does();
    test_image_button_takes_the_status_line();
    test_media_forget_lets_the_viewer_decode_at_its_own_size();

    test_update_release_of();
    test_update_asset_prefix();
    test_update_paths();
    test_update_state_codec();
    test_update_stage_verifies_from_disk();
    test_update_install_happy_path();
    test_update_survives_power_loss_anywhere();
    test_update_asset_ok();
    test_update_signature_gate();
    test_cdn_url();
    test_updater_eligibility();
    test_updater_transitions();
    test_update_screen_asks_before_it_installs();
    test_update_recover_rejects_a_damaged_staged_file();
    test_update_recover_without_a_journal();

    printf("%d checks, %d failures\n", s_checks, s_failures);
    return s_failures ? 1 : 0;
}
