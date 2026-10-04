#include "app/app.h"
#include "app/signin.h"
#include "app/timeline.h"
#include "atproto/errors.h"
#include "store/session_codec.h"
#include "util/log.h"
#include "store/session_store.h"
#include "store/settings_codec.h"
#include "store/settings_store.h"
#include "gfx/canvas.h"
#include "input/input.h"
#include "ui/layout.h"
#include "ui/wrap.h"
#include "util/timefmt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
    CHECK(app.menu.count == 9);
    CHECK(app.menu.items[3].kind == INDIGO_MENU_FIND_POSTS);
    CHECK(app.menu.items[4].kind == INDIGO_MENU_LISTS);
    CHECK(app.menu.items[5].kind == INDIGO_MENU_FEEDS);
    CHECK(app.menu.items[7].kind == INDIGO_MENU_SIGN_OUT);
    CHECK(strcmp(app.menu.items[6].label, "My profile") == 0);

    /* Sign out is the eighth item, so it sits below the window until the
     * selection is moved onto it. */
    for (unsigned i = 0; i < 7; i++) {
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
    CHECK(!s.diagnostics);
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
    in.theme = INDIGO_THEME_DARK;
    in.text_scale = INDIGO_TEXT_SCALE_LARGE;
    in.reduce_motion = true;
    in.high_contrast = true;
    in.large_targets = true;
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
        const char *partial = "indigo-settings 1\nlarge_targets=1\nend\n";

        CHECK(indigo_settings_decode(partial, strlen(partial), &out) ==
              INDIGO_CODEC_OK);
        CHECK(out.large_targets);
        CHECK(out.theme == INDIGO_THEME_AUTO);
        CHECK(out.text_scale == INDIGO_TEXT_SCALE_NORMAL);
        CHECK(out.default_feed[0] == '\0');
    }

    /* One unusable value must not cost the user the rest of the file. */
    {
        const char *mixed = "indigo-settings 1\ntheme=9\ntext_scale=101\n"
                            "high_contrast=yes\nlarge_targets=1\nend\n";

        CHECK(indigo_settings_decode(mixed, strlen(mixed), &out) ==
              INDIGO_CODEC_OK);
        CHECK(out.theme == INDIGO_THEME_AUTO);              /* out of range */
        CHECK(out.text_scale == INDIGO_TEXT_SCALE_NORMAL);  /* in range, but
                                                             * not a scale */
        CHECK(!out.high_contrast);                          /* not a boolean */
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
test_failures(void)
{

    for (int f = INDIGO_FAIL_BAD_CREDENTIALS; f <= INDIGO_FAIL_OTHER; f++) {
        const char *m = indigo_failure_message((indigo_failure) f);

        CHECK(m[0] != '\0');
        CHECK(strlen(m) < INDIGO_STATUS_MAX);
        CHECK(indigo_failure_tag((indigo_failure) f)[0] != '\0');
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
    /* Three facet targets first, then the eight app actions. */
    CHECK(menu.count == 12);
    CHECK(menu.items[0].kind == INDIGO_MENU_OPEN_MENTION);
    CHECK(strcmp(menu.items[0].label, "Profile: @alice.example.com") == 0);
    CHECK(strcmp(menu.items[0].payload, "did:plc:alice0000000000000000000000") == 0);
    CHECK(menu.items[1].kind == INDIGO_MENU_SHOW_TAG);
    CHECK(strcmp(menu.items[1].label, "Tag: #cats") == 0);
    CHECK(menu.items[2].kind == INDIGO_MENU_SHOW_LINK);
    CHECK(strcmp(menu.items[2].label, "Link: https://example.com/x") == 0);
    CHECK(menu.items[3].kind == INDIGO_MENU_COMPOSE);
    CHECK(menu.items[11].kind == INDIGO_MENU_CLOSE);

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
    CHECK(app.menu.count == 12);

    in = (indigo_input) {0};
    in.confirm = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_PROFILE);
    CHECK(indigo_app_take_request(&app, &f) == INDIGO_REQUEST_PROFILE);
    CHECK(strcmp(app.request_post_uri, "did:plc:alice0000000000000000000000") == 0);
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
    CHECK(menu.count == 9);
    CHECK(menu.items[0].kind == INDIGO_MENU_COMPOSE);

    /* Byte ranges past the end of the text are ignored, not read out of
     * bounds. */
    p.facets[0] = (indigo_post_facet) {INDIGO_FACET_LINK, 400, 900, "https://example.com"};
    p.text[sizeof p.text - 1] = '\0';
    indigo_menu_build(&menu, &p, "me.example.com");
    CHECK(menu.count == 9);

    /* An empty account does not claim to know whose profile it is. */
    indigo_menu_build(&menu, NULL, "");
    CHECK(menu.count == 9);
    CHECK(strcmp(menu.items[6].label, "Your profile") == 0);
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
        /* Eight facets plus the six app actions, and no more than the cap. */
        CHECK(menu.count == INDIGO_POST_FACETS_MAX + 6);
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
        INDIGO_SCREEN_COMPOSE, INDIGO_SCREEN_SEARCH};
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

static void
test_text_stays_on_screen(void)
{
    static const indigo_screen screens[] = {
        INDIGO_SCREEN_SIGNIN,     INDIGO_SCREEN_HOME,      INDIGO_SCREEN_THREAD,
        INDIGO_SCREEN_PROFILE,    INDIGO_SCREEN_NOTIFICATIONS, INDIGO_SCREEN_MENU,
        INDIGO_SCREEN_COMPOSE,    INDIGO_SCREEN_SEARCH};
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
    indigo_app_search_loaded(&app, actors, 2);
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
    indigo_app_search_loaded(&app, actors, 2);
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

    indigo_app_search_loaded(&app, NULL, 0);
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
    indigo_app_search_loaded(&app, many, sizeof many / sizeof many[0]);
    CHECK(app.search.count == INDIGO_SEARCH_MAX);
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

/* seenAt is the only place Indigo needs a wire timestamp, so the formatter is
 * pinned against known epochs rather than "now". */
static void
test_time_rfc3339(void)
{
    char buf[32];

    CHECK(indigo_time_format_rfc3339(0, buf, sizeof buf));
    CHECK(strcmp(buf, "1970-01-01T00:00:00Z") == 0);

    CHECK(indigo_time_format_rfc3339(1791055895, buf, sizeof buf));
    CHECK(strcmp(buf, "2026-10-03T19:31:35Z") == 0);

    /* The last second before a leap day, then the leap day itself: the only
     * date arithmetic here is gmtime's, but it is the part that would break. */
    CHECK(indigo_time_format_rfc3339(1709164800, buf, sizeof buf));
    CHECK(strcmp(buf, "2024-02-29T00:00:00Z") == 0);

    /* Too small to hold the result is refused rather than truncated, because a
     * half-written timestamp would be sent to the server as if it were whole. */
    CHECK(!indigo_time_format_rfc3339(0, buf, 8));
    CHECK(buf[0] == '\0');
    CHECK(!indigo_time_format_rfc3339(0, NULL, sizeof buf));
    CHECK(!indigo_time_format_rfc3339(0, buf, 0));
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
    indigo_app_search_loaded(&app, a, 1);
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
    indigo_app_post_search_loaded(&app, p, 2);
    CHECK(app.search.count == 2);
    CHECK(app.search.searched);
    CHECK(indigo_search_selected_post(&app.search) != NULL);
    CHECK(strcmp(indigo_search_selected_post(&app.search)->text, "Rivers before roads.") == 0);
    CHECK(indigo_search_row_post(&app.search, 0, INDIGO_SEARCH_ROWS) != NULL);

    /* "No posts matched" is distinct from not having searched. */
    indigo_app_post_search_loaded(&app, NULL, 0);
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
    indigo_app_post_search_loaded(&app, p, 1);
    CHECK(app.search.count == 1);
    CHECK(!app.search.loading);
    CHECK(strcmp(indigo_search_selected_post(&app.search)->text, "Rivers before roads.") == 0);

    /* Opening someone else's posts drops the first person's. */
    indigo_app_open_author_posts(&app, "someone.else.example");
    CHECK(app.search.count == 0);
    CHECK(strcmp(app.search.subject, "someone.else.example") == 0);
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
    indigo_app_lists_loaded(&app, lists, 2);
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
    indigo_app_search_loaded(&app, members, 2);
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
    indigo_app_feeds_loaded(&app, feeds, 2);
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

int
main(void)
{
    test_canvas_basics();
    test_canvas_overflow_is_bounded();
    test_app_navigation();
    test_touch_navigation();
    test_buttons_spaced_and_on_screen();
    test_layout_invariants();
    test_thread_navigation();
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
    test_menu_rows_on_screen();
    test_search_model();
    test_search_selection_scroll();
    test_search_flow();
    test_search_query_resets_results();
    test_search_empty_and_failure();
    test_search_results_bounded();
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
    test_time_rfc3339();
    test_text_stays_on_screen();

    printf("%d checks, %d failures\n", s_checks, s_failures);
    return s_failures ? 1 : 0;
}
