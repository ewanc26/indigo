#include "app/app.h"
#include "gfx/canvas.h"
#include "input/input.h"
#include "ui/layout.h"

#include <stdio.h>
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
    CHECK(app.screen == INDIGO_SCREEN_HOME);

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
    CHECK(indigo_layout_hit(0, 0) == INDIGO_ACTION_NONE);

    /* The gap between pills is dead space, not a target. */
    CHECK(indigo_layout_hit((int) (a.x + a.w + 5), (int) a.y + 5) == INDIGO_ACTION_NONE);
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
        CHECK(count_text(&top, "B  ") + count_text(&bottom, "B  ") == 1);
        CHECK(count_text(&top, "A  ") + count_text(&bottom, "A  ") == 1);
    }
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

    printf("%d checks, %d failures\n", s_checks, s_failures);
    return s_failures ? 1 : 0;
}
