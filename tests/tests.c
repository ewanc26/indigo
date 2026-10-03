#include "app/app.h"
#include "app/signin.h"
#include "app/timeline.h"
#include "atproto/errors.h"
#include "store/session_codec.h"
#include "util/log.h"
#include "store/session_store.h"
#include "gfx/canvas.h"
#include "input/input.h"
#include "ui/layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    in.confirm = true;
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_PROFILE);

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
    indigo_rect profile = indigo_layout_button_rect(INDIGO_ACTION_PROFILE);
    indigo_rect home = indigo_layout_button_rect(INDIGO_ACTION_HOME);

    indigo_app_init(&app);
    app.screen = INDIGO_SCREEN_HOME;
    in.touch_pressed = true;
    in.touch_x = (int) (profile.x + profile.w / 2);
    in.touch_y = (int) (profile.y + profile.h / 2);
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_PROFILE);

    in.touch_x = (int) (home.x + home.w / 2);
    in.touch_y = (int) (home.y + home.h / 2);
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_HOME);

    /* A touch that is held but not newly pressed must not re-trigger. */
    in.touch_pressed = false;
    in.touch_x = (int) (profile.x + 1);
    in.touch_y = (int) (profile.y + 1);
    indigo_app_update(&app, &in);
    CHECK(app.screen == INDIGO_SCREEN_HOME);
}

static void
test_buttons_spaced_and_on_screen(void)
{
    indigo_rect a = indigo_layout_button_rect(INDIGO_ACTION_PROFILE);
    indigo_rect b = indigo_layout_button_rect(INDIGO_ACTION_HOME);

    CHECK(a.x >= 0 && a.x + a.w <= INDIGO_BOTTOM_WIDTH);
    CHECK(b.x >= 0 && b.x + b.w <= INDIGO_BOTTOM_WIDTH);
    CHECK(a.y + a.h <= INDIGO_BOTTOM_HEIGHT);
    CHECK(b.x - (a.x + a.w) >= 16.0f);
    CHECK(a.h >= 40.0f && b.h >= 40.0f);
    CHECK(indigo_layout_hit(INDIGO_SCREEN_HOME, 0, 0) == INDIGO_ACTION_NONE);

    /* The gap between pills is dead space, not a target. */
    CHECK(indigo_layout_hit(INDIGO_SCREEN_HOME, (int) (a.x + a.w + 5), (int) a.y + 5) == INDIGO_ACTION_NONE);
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

    for (int screen = 0; screen <= INDIGO_SCREEN_PROFILE; screen++) {
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

        /* One hint per control: START appears once, and back is hinted once. */
        CHECK(count_text(&top, "START") + count_text(&bottom, "START") == 1);
        if (screen != INDIGO_SCREEN_SIGNIN) {
            CHECK(count_text(&top, "B  ") + count_text(&bottom, "B  ") == 1);
        }
        CHECK(count_text(&top, "A  ") + count_text(&bottom, "A  ") == 1);
    }
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

    in = (indigo_input) {0};
    r = indigo_layout_button_rect(INDIGO_ACTION_SIGN_OUT);
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

    system("mkdir -p build-host");
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

int
main(void)
{
    test_canvas_basics();
    test_canvas_overflow_is_bounded();
    test_app_navigation();
    test_touch_navigation();
    test_buttons_spaced_and_on_screen();
    test_layout_invariants();
    test_normalise_service();
    test_normalise_handle();
    test_signin_fields();
    test_signin_flow();
    test_signin_targets_spaced();
    test_session_codec();
    test_session_store();
    test_failures();
    test_log_file();
    test_autofill();
    test_timeline_bounds();
    test_timeline_selection();
    test_timeline_paging();
    test_timeline_actions();
    test_copy_utf8();

    printf("%d checks, %d failures\n", s_checks, s_failures);
    return s_failures ? 1 : 0;
}
