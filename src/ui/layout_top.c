/* The top screen of each app screen. */

#include "ui/layout_internal.h"

static void
build_top_signin(const indigo_app *app, indigo_canvas *c)
{
    const indigo_signin *s = &app->signin;

    indigo_canvas_text(c, 18, 98, 0.8f, COL_TEXT, "Sign in");
    indigo_canvas_text(c, 18, 126, 0.6f, COL_TEXT_DIM,
                       "Empty password = browser sign-in.");
    indigo_canvas_text(c, 18, 144, 0.6f, COL_TEXT_DIM,
                       "The PDS handles your password + MFA.");

#if defined(__3DS__)
    if (indigo_session_pair_url()[0]) {
        const char *url = indigo_session_pair_url();
        const char *code = indigo_session_pair_code();
        indigo_canvas_text(c, 18, 166, 0.55f, COL_TEXT_SOFT,
                           "Open on your phone/computer:");
        indigo_layout_text_fitted(c, 18, 182, 0.45f, COL_TEXT, 364.0f, url);
        indigo_canvas_text(c, 18, 196, 0.55f, COL_TEXT_SOFT,
                           "Pair code: %s", code);
    } else
#endif
    if (s->status[0]) {
        indigo_layout_text_fitted(c, 18, 172, 0.65f,
                                  s->status_is_error ? COL_ERROR : COL_TEXT_SOFT,
                                  364.0f, s->status);
    }
    indigo_canvas_text(c, 18, 208, 0.6f, COL_TEXT_DIM, "A  Sign in or edit   START  Exit");
}

static void
build_top_profile(const indigo_app *app, indigo_canvas *c)
{
    const indigo_profile *p = &app->profile;
    const bool self = indigo_app_profile_is_self(app);

    indigo_layout_top_title(c, "Profile",
                            self ? "A  Edit profile" : "Y  Follow   X  Mute   R  Block");
    if (!p->loaded) {
        indigo_canvas_text(c, 18, 60, 0.75f, COL_TEXT_SOFT, "%s",
                           p->loading ? "Loading profile..." : "Profile not loaded.");
        if (p->handle[0]) {
            indigo_layout_handle_fitted(c, 18, 90, 0.6f, COL_TEXT_DIM, 364.0f, p->handle);
        }
        if (p->status[0]) {
            indigo_canvas_text(c, 18, 116, 0.6f, p->status_is_error ? COL_ERROR : COL_TEXT_DIM,
                               "%s", p->status);
        }
        return;
    }
    indigo_layout_draw_avatar(c, p->avatar, 14, 38, PROFILE_AVATAR);
    /* The name ends inside the screen: a 40px avatar plus a long name is wider
     * than the 400px line, and `%.26s` assumed one byte was one character, so
     * a CJK name could be twice the width it was cut for. */
    indigo_layout_text_fitted(c, 64, 44, 0.85f, COL_TEXT, 328.0f,
                              p->display_name[0] ? p->display_name : p->handle);
    {
        char line[INDIGO_HANDLE_MAX + 16];

        snprintf(line, sizeof line, "@%s%s", p->handle,
                 !self && p->following ? "   Following" : "");
        indigo_layout_text_fitted(c, 64, 72, 0.6f, COL_TEXT_DIM, 328.0f, line);
    }
    indigo_layout_top_paragraph(c, 18, 98, 0.6f, COL_TEXT_SOFT, 4, p->bio);
    indigo_canvas_text(c, 18, 196, 0.6f, COL_TEXT, "%u posts", p->posts);
    indigo_canvas_text(c, 128, 196, 0.6f, COL_TEXT, "%u followers", p->followers);
    indigo_canvas_text(c, 262, 196, 0.6f, COL_TEXT, "%u following", p->follows);
}

static void
build_top_profile_edit(const indigo_app *app, indigo_canvas *c)
{
    const indigo_profile_edit *e = &app->profile_edit;

    indigo_layout_top_title(c, "Edit profile", "A  Edit   Y  Save   B  Back");
    indigo_canvas_rect(c, 12, 46, 376, 52, e->selected == 0 ? COL_PILL_ACTIVE : COL_PILL);
    indigo_canvas_text(c, 20, 50, 0.55f, COL_TEXT_SOFT, "Display name");
    indigo_layout_text_fitted(c, 20, 68, 0.75f, COL_TEXT, 368.0f,
                              e->display_name[0] ? e->display_name : "(not set)");

    indigo_canvas_rect(c, 12, 106, 376, 92, e->selected == 1 ? COL_PILL_ACTIVE : COL_PILL);
    indigo_canvas_text(c, 20, 110, 0.55f, COL_TEXT_SOFT, "Bio");
    if (e->bio[0]) {
        indigo_layout_top_paragraph(c, 20, 128, 0.6f, COL_TEXT, 3, e->bio);
    } else {
        indigo_canvas_text(c, 20, 128, 0.6f, COL_TEXT_DIM, "(not set)");
    }
    if (e->status[0]) {
        indigo_canvas_text(c, 18, 206, 0.6f, e->status_is_error ? COL_ERROR : COL_TEXT_SOFT,
                           "%s", e->status);
    }
}

static void
build_top_notifications(const indigo_app *app, indigo_canvas *c)
{
    const indigo_notifications *n = &app->notifications;
    const indigo_notification *sel = indigo_notifications_selected(n);

    indigo_layout_top_title(c, "Notifications", "B  Back   SEL  Reload");
    if (!sel) {
        indigo_canvas_text(c, 18, 90, 0.75f, COL_TEXT_SOFT, "%s",
                           n->loading ? "Loading notifications..." : "Nothing to show.");
        if (n->status[0]) {
            indigo_canvas_text(c, 18, 120, 0.6f, n->status_is_error ? COL_ERROR : COL_TEXT_DIM,
                               "%s", n->status);
        }
        return;
    }
    indigo_canvas_text(c, 330, 36, 0.55f, COL_TEXT_DIM, "%u / %u", n->selected + 1, n->count);
    indigo_layout_draw_avatar(c, sel->avatar, 12, 46, HEAD_AVATAR);
    indigo_layout_text_fitted(c, 44, 48, 0.75f, COL_TEXT, 348.0f,
                              sel->name[0] ? sel->name : sel->handle);
    indigo_layout_handle_fitted(c, 44, 74, 0.6f, COL_TEXT_DIM, 348.0f, sel->handle);
    indigo_canvas_text(c, 18, 96, 0.65f, sel->unread ? COL_LINK : COL_TEXT_SOFT, "%s%s",
                       indigo_layout_note_verb(sel->kind), sel->unread ? "  (new)" : "");
    if (sel->text[0]) {
        indigo_layout_top_paragraph(c, 18, 124, 0.6f, COL_TEXT, 4, sel->text);
    }
}

static void
build_top_queue(const indigo_app *app, indigo_canvas *c)
{
    const indigo_post_queue *q = &app->queue;

    indigo_layout_top_title(c, "Queued posts", "B  Back   A  Send   Y  Discard");
    if (q->count == 0) {
        indigo_canvas_text(c, 18, 90, 0.75f, COL_TEXT_SOFT, "Nothing is queued.");
    } else {
        const indigo_queued_post *p = &q->items[app->queue_selected < q->count ? app->queue_selected : 0];

        indigo_canvas_text(c, 330, 36, 0.55f, COL_TEXT_DIM, "%u / %u",
                           app->queue_selected + 1, q->count);
        indigo_layout_top_paragraph(c, 18, 60, 0.75f, COL_TEXT, 6, p->text);
    }
    if (app->queue_status[0]) {
        indigo_canvas_text(c, 18, 200, 0.6f, app->queue_status_is_error ? COL_ERROR : COL_TEXT_DIM,
                           "%s", app->queue_status);
    }
}

static void
build_top_drafts(const indigo_app *app, indigo_canvas *c)
{
    const indigo_saved_draft *d =
        indigo_saved_drafts_get(&app->drafts, app->draft_selected);
    char context[INDIGO_POST_NOTE_MAX];

    indigo_layout_top_title(c, "Drafts", "B  Back   A  Resume   Y  Delete");
    if (!d) {
        indigo_canvas_text(c, 18, 90, 0.75f, COL_TEXT_SOFT, "No saved drafts.");
        indigo_canvas_text(c, 18, 118, 0.6f, COL_TEXT_DIM,
                           "Use Save draft on the compose screen.");
    } else {
        indigo_canvas_text(c, 330, 36, 0.55f, COL_TEXT_DIM, "%u / %u",
                           app->draft_selected + 1, app->drafts.count);
        indigo_layout_top_paragraph(c, 18, 54, 0.7f, COL_TEXT, 5, d->text);
        if (!d->has_target) {
            snprintf(context, sizeof context, "New post");
        } else if (d->mode == INDIGO_COMPOSE_REPLY) {
            snprintf(context, sizeof context, "Reply to @%.40s", d->target_handle);
        } else {
            snprintf(context, sizeof context, "Quote of @%.40s", d->target_handle);
        }
        indigo_layout_text_fitted(c, 18, 176, 0.6f, COL_TEXT_SOFT, 364.0f, context);
    }
    if (app->draft_status[0]) {
        indigo_canvas_text(c, 18, 204, 0.55f,
                           app->draft_status_is_error ? COL_ERROR : COL_TEXT_DIM,
                           "%.54s", app->draft_status);
    }
}

static void
build_top_requests(const indigo_app *app, indigo_canvas *c)
{
    const indigo_chat_inbox *r = &app->requests;
    const indigo_chat_convo *sel = r->selected < r->count ? &r->items[r->selected] : NULL;

    indigo_layout_top_title(c, "Requests", "A Accept  X Decline  B Back");
    if (!sel) {
        indigo_canvas_text(c, 18, 90, 0.75f, COL_TEXT_SOFT, "%s",
                           r->loading ? "Loading requests..." : "No requests.");
        if (r->status[0]) {
            indigo_canvas_text(c, 18, 120, 0.6f, r->status_is_error ? COL_ERROR : COL_TEXT_DIM,
                               "%s", r->status);
        }
        return;
    }
    indigo_canvas_text(c, 330, 36, 0.55f, COL_TEXT_DIM, "%u / %u%s", r->selected + 1, r->count,
                       r->has_more ? "+" : "");
    indigo_layout_text_fitted(c, 44, 48, 0.75f, COL_TEXT, 348.0f,
                              sel->name[0] ? sel->name : sel->handle);
    if (sel->handle[0]) {
        indigo_layout_text_fitted(c, 44, 74, 0.6f, COL_TEXT_DIM, 348.0f, sel->handle);
    }
    if (sel->preview[0]) {
        indigo_layout_top_paragraph(c, 18, 100, 0.6f, COL_TEXT, 4, sel->preview);
    }
    if (r->status[0]) {
        indigo_canvas_text(c, 18, 200, 0.55f, r->status_is_error ? COL_ERROR : COL_TEXT_DIM,
                           "%s", r->status);
    }
}

static void
build_top_messages(const indigo_app *app, indigo_canvas *c)
{
    const indigo_chat_inbox *m = &app->messages;
    const indigo_chat_convo *sel = m->selected < m->count ? &m->items[m->selected] : NULL;
    char requests[32];

    indigo_layout_top_title(c, "Messages", "A Open  X New  ZR Group  Y Pending  B Back");
    if (app->requests_count > 0 && indigo_chat_requests_label(app->requests_count, requests, sizeof requests)) {
        /* Right of the name, left of the N / M counter at x 330. */
        indigo_canvas_text(c, 200, 36, 0.55f, COL_LINK, "%s", requests);
    }
    if (!sel) {
        indigo_canvas_text(c, 18, 90, 0.75f, COL_TEXT_SOFT, "%s",
                           m->loading ? "Loading messages..." : "Nothing to show.");
        if (m->status[0]) {
            indigo_canvas_text(c, 18, 120, 0.6f, m->status_is_error ? COL_ERROR : COL_TEXT_DIM,
                               "%s", m->status);
        }
        return;
    }
    indigo_canvas_text(c, 330, 36, 0.55f, COL_TEXT_DIM, "%u / %u", m->selected + 1, m->count);
    indigo_layout_text_fitted(c, 44, 48, 0.75f, COL_TEXT, 348.0f,
                              sel->name[0] ? sel->name : sel->handle);
    if (sel->handle[0]) {
        indigo_layout_text_fitted(c, 44, 74, 0.6f, COL_TEXT_DIM, 348.0f, sel->handle);
    }
    indigo_canvas_text(c, 18, 96, 0.65f, sel->unread ? COL_LINK : COL_TEXT_SOFT, "%s",
                       sel->unread ? "New messages" : "Read");
    if (sel->preview[0]) {
        indigo_layout_top_paragraph(c, 18, 124, 0.6f, COL_TEXT, 4, sel->preview);
    }
    if (m->status[0]) {
        indigo_canvas_text(c, 18, 208, 0.55f, m->status_is_error ? COL_ERROR : COL_TEXT_DIM,
                           "%s", m->status);
    }
}

static void
build_top_chat_thread(const indigo_app *app, indigo_canvas *c)
{
    const indigo_chat_thread *t = &app->chat_thread;
    const indigo_chat_message *sel = t->selected < t->count ? &t->items[t->selected] : NULL;

    indigo_layout_top_title(c, "Messages", app->chat_is_group
                                              ? "B Back  A Reply  Y Send  X Requests  ZR Group"
                                              : "B Back  A Reply  Y Send");
    if (!sel) {
        indigo_canvas_text(c, 18, 90, 0.75f, COL_TEXT_SOFT, "%s",
                           t->loading ? "Loading messages..." : "Nothing to show.");
        if (t->status[0]) {
            indigo_canvas_text(c, 18, 120, 0.6f, t->status_is_error ? COL_ERROR : COL_TEXT_DIM,
                               "%s", t->status);
        }
        return;
    }
    indigo_canvas_text(c, 330, 36, 0.55f, COL_TEXT_DIM, "%u / %u", t->selected + 1, t->count);
    indigo_layout_text_fitted(c, 18, 52, 0.7f, sel->mine ? COL_LINK : COL_TEXT_SOFT,
                              364.0f,
                              sel->mine ? "You" : (t->title[0] ? t->title : "Them"));
    indigo_layout_top_paragraph(c, 18, 84, 0.6f, COL_TEXT, 4, sel->text);
    /* The reply being written, or the last send's result. */
    if (t->reply[0]) {
        /* Sized from the reply itself: a chat message is much longer than a
         * handle, and the fitter decides how much of it is drawn. */
        char line[sizeof t->reply + 8];

        snprintf(line, sizeof line, "Reply: %s", t->reply);
        indigo_layout_text_fitted(c, 18, 200, 0.6f, COL_LINK, 364.0f, line);
    }
    if (t->status[0]) {
        indigo_canvas_text(c, 18, 222, 0.55f, t->status_is_error ? COL_ERROR : COL_TEXT_DIM,
                           "%s", t->status);
    }
}

static void
build_top_chat_group(const indigo_app *app, indigo_canvas *c)
{
    const indigo_chat_group *g = &app->group;
    const indigo_chat_member *sel = g->selected < g->count ? &g->members[g->selected] : NULL;
    char count[32];

    indigo_layout_top_title(c, "Group", "A Rename  Y Add  X Remove  ZR Link  B Back");
    if (!g->loaded) {
        indigo_canvas_text(c, 18, 90, 0.75f, COL_TEXT_SOFT, "%s",
                           app->group_pending ? "Loading group..." : "Group details are unavailable.");
        if (app->group_status[0]) {
            indigo_canvas_text(c, 18, 120, 0.6f,
                               app->group_status_is_error ? COL_ERROR : COL_TEXT_DIM, "%s",
                               app->group_status);
        }
        return;
    }

    snprintf(count, sizeof count, "%d member%s", g->total > 0 ? g->total : (int) g->count,
             (g->total == 1 || (g->total == 0 && g->count == 1)) ? "" : "s");
    indigo_layout_text_fitted(c, 18, 48, 0.8f, COL_TEXT, 364.0f,
                              g->name[0] ? g->name : "Group");
    indigo_canvas_text(c, 18, 72, 0.6f, COL_TEXT_DIM, "%s", count);

    if (g->join_link_url[0]) {
        indigo_canvas_text(c, 18, 96, 0.55f, COL_LINK, "%.44s", g->join_link_url);
        indigo_canvas_text(c, 18, 114, 0.55f, COL_TEXT_DIM, "%s",
                           g->join_link_enabled ? "Join link enabled" : "Join link disabled");
    } else {
        indigo_canvas_text(c, 18, 96, 0.6f, COL_TEXT_SOFT, "No join link yet.");
    }

    if (sel) {
        indigo_canvas_text(c, 18, 138, 0.55f, COL_TEXT_DIM, "Selected member");
        indigo_layout_text_fitted(c, 18, 154, 0.7f, COL_TEXT, 364.0f,
                                  sel->name[0] ? sel->name : sel->handle);
        if (sel->handle[0]) {
            indigo_layout_text_fitted(c, 18, 176, 0.55f, COL_TEXT_DIM, 364.0f,
                                      sel->handle);
        }
    }
    if (app->group_status[0]) {
        indigo_canvas_text(c, 18, 210, 0.55f,
                           app->group_status_is_error ? COL_ERROR : COL_TEXT_DIM, "%s",
                           app->group_status);
    }
}

static void
build_top_chat_join_requests(const indigo_app *app, indigo_canvas *c)
{
    const indigo_chat_join_requests *l = &app->join_requests;
    const indigo_chat_join_request *sel = l->selected < l->count ? &l->items[l->selected] : NULL;

    indigo_layout_top_title(c, "Join requests", "A Approve  X Reject  B Back");
    if (!sel) {
        indigo_canvas_text(c, 18, 90, 0.75f, COL_TEXT_SOFT, "%s",
                           l->loading ? "Loading join requests..." : "No join requests.");
        if (l->status[0]) {
            indigo_canvas_text(c, 18, 120, 0.6f, l->status_is_error ? COL_ERROR : COL_TEXT_DIM,
                               "%s", l->status);
        }
        return;
    }
    indigo_canvas_text(c, 330, 36, 0.55f, COL_TEXT_DIM, "%u / %u%s", l->selected + 1, l->count,
                       l->has_more ? "+" : "");
    indigo_layout_text_fitted(c, 18, 52, 0.75f, COL_TEXT, 364.0f,
                              sel->name[0] ? sel->name : sel->handle);
    if (sel->handle[0]) {
        indigo_canvas_text(c, 18, 78, 0.6f, COL_TEXT_DIM, "%s", sel->handle);
    }
    indigo_canvas_text(c, 18, 108, 0.6f, COL_TEXT_SOFT, "Asked to join this group.");
    if (l->status[0]) {
        indigo_canvas_text(c, 18, 208, 0.55f, l->status_is_error ? COL_ERROR : COL_TEXT_DIM,
                           "%s", l->status);
    }
}

static void
build_top_search(const indigo_app *app, indigo_canvas *c)
{
    const indigo_search *s = &app->search;
    const indigo_actor *sel = indigo_search_selected(s);
    const indigo_post *psel = indigo_search_selected_post(s);
    const indigo_list *lsel = indigo_search_selected_list(s);
    bool posts = indigo_search_is_posts(s);
    bool lists = indigo_search_is_lists(s);

    if (s->list_editing) {
        indigo_layout_top_title(c, s->list_creating ? "New list" : "Edit list",
                                "A  Edit   L/R  Purpose");
        indigo_canvas_text(c, 18, 44, 0.55f,
                           s->list_field == 0 ? COL_TEXT : COL_TEXT_DIM, "Name");
        indigo_canvas_text(c, 18, 60, 0.75f, COL_TEXT, "%.42s",
                           s->edit_name[0] ? s->edit_name : "(not set)");
        indigo_canvas_text(c, 18, 84, 0.55f,
                           s->list_field == 1 ? COL_TEXT : COL_TEXT_DIM,
                           "Description");
        if (s->edit_description[0]) {
            indigo_layout_top_paragraph(c, 18, 100, 0.58f, COL_TEXT, 3,
                                        s->edit_description);
        } else {
            indigo_canvas_text(c, 18, 100, 0.58f, COL_TEXT_DIM, "(not set)");
        }
        indigo_canvas_text(c, 18, 164, 0.55f,
                           s->list_field == 2 ? COL_TEXT : COL_TEXT_DIM,
                           "Purpose");
        indigo_canvas_text(c, 18, 180, 0.7f, COL_TEXT, "%s",
                           indigo_list_purpose_label(s->edit_purpose));
        if (s->list_status[0]) {
            indigo_canvas_text(c, 18, 214, 0.55f,
                               s->list_status_error ? COL_ERROR : COL_TEXT_DIM,
                               "%.56s", s->list_status);
        }
        return;
    }

    /* SEL opens whatever the row is: a profile for a person, a thread for a
     * post, a member list for a curated list. All read as "Open" here, so the
     * hint is stated once. The lists have nothing to type, so the A hint is
     * only offered where the header box takes typing. */
    indigo_layout_top_title(c, indigo_search_title(s),
              indigo_search_is_typed(s) ? "A  Type   SEL  Open" : "SEL  Open");
    if (s->kind == INDIGO_SEARCH_STARTER_PACK) {
        const char *feed = indigo_search_selected_feed(s);

        if (s->loading && s->count == 0) {
            indigo_canvas_text(c, 18, 90, 0.75f, COL_TEXT_SOFT, "Loading starter pack...");
            return;
        }
        if (s->pack.creator_handle[0]) {
            indigo_canvas_text(c, 18, 52, 0.65f, COL_TEXT_DIM, "by @%.34s",
                               s->pack.creator_handle);
        }
        if (s->pack.description[0]) {
            indigo_layout_top_paragraph(c, 18, 78, 0.58f, COL_TEXT_SOFT, 3,
                                        s->pack.description);
        }
        indigo_canvas_text(c, 18, 168, 0.55f, COL_TEXT_DIM,
                           "%u members  %u feeds",
                           s->pack.member_count ? s->pack.member_count : s->count,
                           s->pack.feed_count);
        if (s->status[0] && s->count == 0 && s->pack.feed_count == 0) {
            indigo_canvas_text(c, 18, 196, 0.6f, s->status_is_error ? COL_ERROR : COL_TEXT_SOFT,
                               "%.44s", s->status);
        } else if (feed) {
            indigo_canvas_text(c, 18, 192, 0.65f, COL_TEXT, "Included feed");
            indigo_canvas_text(c, 18, 212, 0.5f, COL_TEXT_DIM, "%.48s", feed);
            indigo_canvas_text(c, 330, 218, 0.5f, COL_TEXT_SOFT, "SEL Open feed");
        } else if (sel) {
            indigo_canvas_text(c, 18, 192, 0.65f, COL_TEXT, "%.34s",
                               sel->display_name[0] ? sel->display_name : sel->handle);
            indigo_canvas_text(c, 18, 212, 0.55f, COL_TEXT_DIM, "@%.38s", sel->handle);
            indigo_canvas_text(c, 330, 218, 0.5f, COL_TEXT_SOFT, "SEL Open profile");
        }
        if (indigo_search_row_count(s) > 0) {
            indigo_canvas_text(c, 330, 36, 0.55f, COL_TEXT_DIM, "%u / %u",
                               s->selected + 1, indigo_search_row_count(s));
        }
        return;
    }
    if (s->loading) {
        indigo_canvas_text(c, 18, 90, 0.75f, COL_TEXT_SOFT, "Searching...");
        return;
    }
    if (posts ? !psel : lists ? !lsel : !sel) {
        if (s->status[0]) {
            indigo_canvas_text(c, 18, 84, 0.7f, s->status_is_error ? COL_ERROR : COL_TEXT_SOFT,
                               "%.40s", s->status);
        } else if (!s->searched) {
            /* Reached only for the two search modes: every other kind is
             * requested on open, so those arrive loading, loaded or failed. */
            indigo_canvas_text(c, 18, 76, 0.7f, COL_TEXT_SOFT,
                               posts ? "Search posts by words in their text."
                                     : "Find people by name or handle.");
            if (indigo_search_is_typed(s)) {
                indigo_canvas_text(c, 18, 104, 0.6f, COL_TEXT_DIM,
                                   "Press A, or tap the box, to type.");
            }
        } else {
            indigo_canvas_text(c, 18, 90, 0.7f, COL_TEXT_SOFT, "No results.");
        }
        return;
    }
    indigo_canvas_text(c, 330, 36, 0.55f, COL_TEXT_DIM, "%u / %u", s->selected + 1, s->count);
    if (lists && s->kind == INDIGO_SEARCH_LISTS) {
        indigo_layout_text_fitted(c, 18, 52, 0.85f, COL_TEXT, 364.0f, lsel->name);
        if (lsel->description[0]) {
            indigo_layout_top_paragraph(c, 18, 82, 0.6f, COL_TEXT_SOFT, 3, lsel->description);
        }
        indigo_layout_text_fitted(c, 18, 196, 0.55f, COL_TEXT_DIM, 364.0f, lsel->uri);
        indigo_canvas_text(c, 18, 214, 0.6f, COL_TEXT_SOFT, "SEL  Open members");
        if (s->list_status[0]) {
            indigo_canvas_text(c, 18, 228, 0.5f,
                               s->list_status_error ? COL_ERROR : COL_TEXT_DIM,
                               "%.60s", s->list_status);
        }
        return;
    }
    if (lists && s->kind == INDIGO_SEARCH_FEEDS) {
        indigo_layout_text_fitted(c, 18, 52, 0.85f, COL_TEXT, 364.0f, lsel->name);
        indigo_canvas_text(c, 18, 82, 0.6f, COL_TEXT_SOFT, "Custom feed");
        indigo_layout_text_fitted(c, 18, 196, 0.55f, COL_TEXT_DIM, 364.0f, lsel->uri);
        indigo_canvas_text(c, 18, 214, 0.6f, COL_TEXT_SOFT, "SEL  Open feed");
        return;
    }
    if (posts) {
        indigo_layout_text_fitted(c, 18, 52, 0.75f, COL_TEXT, 364.0f,
                                  indigo_layout_author_name(psel));
        indigo_layout_handle_fitted(c, 18, 76, 0.55f, COL_TEXT_DIM, 364.0f, psel->handle);
        indigo_layout_draw_post_body(c, psel, app->settings.alt_text, app->settings.text_scale);
        indigo_canvas_text(c, 18, POST_COUNTER_Y, 0.55f, COL_TEXT_DIM, "%u replies",
                           psel->reply_count);
        indigo_canvas_text(c, 118, POST_COUNTER_Y, 0.55f, COL_TEXT_DIM, "%u reposts",
                           psel->repost_count);
        indigo_canvas_text(c, 218, POST_COUNTER_Y, 0.55f, COL_TEXT_DIM, "%u likes",
                           psel->like_count);
        return;
    }
    indigo_layout_text_fitted(c, 18, 52, 0.85f, COL_TEXT, 364.0f,
                              sel->display_name[0] ? sel->display_name : sel->handle);
    indigo_layout_handle_fitted(c, 18, 82, 0.65f, COL_TEXT_DIM, 364.0f, sel->handle);
    indigo_layout_text_fitted(c, 18, 112, 0.55f, COL_TEXT_DIM, 364.0f, sel->did);
    indigo_canvas_text(c, 18, 140, 0.6f, COL_TEXT_SOFT, "SEL  Open profile");
    if (s->kind == INDIGO_SEARCH_LIST_MEMBERS && s->list_status[0]) {
        indigo_canvas_text(c, 18, 214, 0.55f,
                           s->list_status_error ? COL_ERROR : COL_TEXT_DIM,
                           "%.60s", s->list_status);
    }
}

static void
build_top_menu(const indigo_app *app, indigo_canvas *c)
{
    char note[200];

    if (app->menu.showing_link) {
        if (app->menu.qr_size > 0) {
            unsigned modules = (unsigned) app->menu.qr_size;
            unsigned px = (unsigned) (INDIGO_TOP_HEIGHT - 48) / (modules + 8);
            unsigned side;
            if (px > 4) px = 4;
            if (px == 0) px = 1;
            side = (modules + 8) * px;
            indigo_layout_top_title(c, "Open link on phone", "B  Back");
            indigo_canvas_qr(c, (INDIGO_TOP_WIDTH - (float) side) / 2.0f,
                             44.0f + (INDIGO_TOP_HEIGHT - 44.0f - (float) side) / 2.0f,
                             px, modules, app->menu.qr);
        } else {
            indigo_layout_top_title(c, "Link", "B  Back");
            indigo_canvas_text(c, 18, 72, 0.65f, COL_TEXT, "Could not encode this URL.");
            indigo_canvas_text(c, 18, 100, 0.55f, COL_TEXT_DIM, "Use the address on the bottom screen.");
        }
        return;
    }
    if (app->menu.picking_image) {
        indigo_layout_top_title(c, "Add image", "B  Close");
        indigo_canvas_text(c, 18, 56, 0.6f, COL_TEXT_SOFT, "Choose a picture from");
        indigo_canvas_text(c, 18, 78, 0.6f, COL_TEXT, "%s", INDIGO_IMAGES_DIR);
        indigo_canvas_text(c, 18, 100, 0.6f, COL_TEXT, "and %s (camera)", INDIGO_CAMERA_DIR);
        snprintf(note, sizeof note,
                 "Copy .jpg or .png files there from a computer; each must be under %d KB. "
                 "You can describe the picture for people who cannot see it after choosing it.",
                 WF_ATTACH_MAX_BYTES / 1000);
        indigo_layout_top_paragraph(c, 18, 124, 0.55f, COL_TEXT_DIM, 4, note);
        return;
    }
    indigo_layout_top_title(c, "Menu", "B  Close");
    indigo_canvas_text(c, 18, 64, 0.7f, COL_TEXT_SOFT, "Signed in as");
    {
        char fitted[INDIGO_HANDLE_MAX];
        unsigned units = (unsigned) ((INDIGO_TOP_WIDTH - 36) /
                                     (INDIGO_CHAR_WIDTH * 0.8f));

        indigo_fit_text(fitted, sizeof fitted, app->signin.account, units);
        indigo_canvas_text(c, 18, 90, 0.8f, COL_TEXT, "%s", fitted);
    }
    indigo_canvas_text(c, 18, 150, 0.6f, COL_TEXT_DIM, "Wolfram: %s",
                       app->wolfram_linked ? "linked" : "not linked");
    indigo_canvas_text(c, 18, 208, 0.6f, COL_TEXT_DIM, "START  Exit");
}

static void
build_top_compose(const indigo_app *app, indigo_canvas *c)
{
    const indigo_compose *d = &app->compose;
    float y = 44;

    indigo_layout_top_title(c, indigo_layout_compose_title(d), "A  Write   B  Back");
    if (d->has_target) {
        char line[INDIGO_HANDLE_MAX + 24];

        snprintf(line, sizeof line, "%s @%s",
                 d->mode == INDIGO_COMPOSE_QUOTE ? "Quoting" : "Replying to",
                 d->target.handle);
        indigo_layout_text_fitted(c, 18, y, 0.55f, COL_TEXT_DIM, 364.0f, line);
        y = indigo_layout_top_paragraph(c, 18, y + 18, 0.55f, COL_TEXT_DIM, 2, d->target.text) + 8;
    }
    if (d->text[0]) {
        indigo_layout_top_paragraph(c, 18, y + 4, 0.65f, COL_TEXT, 5, d->text);
    } else {
        indigo_canvas_text(c, 18, y + 4, 0.65f, COL_TEXT_DIM, "Tap the box below to write.");
    }
    indigo_canvas_text(c, 18, 214, 0.55f, indigo_layout_utf8_length(d->text) > 300 ? COL_ERROR : COL_TEXT_DIM,
                       "%u / 300", indigo_layout_utf8_length(d->text));
    /* The pill on the bottom screen is the control; this is the reading of it,
     * where there is room to spell the rule out. */
    if (indigo_compose_can_gate(d)) {
        indigo_canvas_text(c, 18, 188, 0.6f, COL_TEXT_DIM, "Replies: %s",
                           indigo_compose_gate_label(d));
    }
    if (indigo_compose_has_image(d)) {
        indigo_canvas_text(c, 18, 170, 0.6f, COL_TEXT_DIM, "Image: %.30s%s", indigo_compose_image_name(d),
                           d->image_alt[0] ? " (alt text set)" : " (no alt text)");
    }
    if (d->status[0]) {
        indigo_canvas_text(c, 120, 214, 0.55f, d->status_is_error ? COL_ERROR : COL_TEXT_SOFT,
                           "%.50s", d->status);
    } else if (d->sending) {
        indigo_canvas_text(c, 120, 214, 0.55f, COL_TEXT_SOFT, "Posting...");
    }
}

/* Credits is a reference page rather than a list to act on: each project and
 * its full repository address share a line, so the address can be read and
 * typed elsewhere. The 3DS cannot open a browser. */
static void
build_top_credits(const indigo_app *app, indigo_canvas *c)
{
    (void) app;
    indigo_layout_top_title(c, "Credits", "B  Back");
    indigo_canvas_text(c, 16, 42, 0.55f, COL_TEXT_DIM,
                       "The repositories behind the stack.");
    indigo_canvas_text(c, 16, 56, 0.55f, COL_TEXT_DIM,
                       "Full addresses, for typing elsewhere.");

    for (unsigned i = 0; i < indigo_credit_repo_count(); i++) {
        const indigo_credit_repo *repo = indigo_credit_repo_at(i);
        float y = 78.0f + 26.0f * (float) i;

        indigo_canvas_text(c, 16, y, 0.55f, COL_TEXT, "%s", repo->name);
        indigo_canvas_text(c, 124, y, 0.43f, COL_TEXT_SOFT, "%s", repo->url);
    }

    indigo_canvas_text(c, 16, 214, 0.5f, COL_TEXT_DIM,
                       "All five repositories are public.");
}

static void
build_top_settings(const indigo_app *app, indigo_canvas *c)
{
    unsigned row = app->settings_selected < 8 ? app->settings_selected : 0;
    char value[64];

    indigo_layout_top_title(c, "Settings", "");
    indigo_layout_setting_value(app, row, value, sizeof value);

    /* One setting at a time: eight values at 0.65 scale left no room for what
     * any of them meant. The selected tile gets a heading, its value and a
     * short explanation, while the bottom screen keeps every option visible. */
    indigo_canvas_text(c, 18, 44, 0.9f, COL_TEXT, "%s",
                       indigo_layout_setting_label(row));
    indigo_canvas_text(c, 18, 78, 0.75f, COL_TEXT, "%s", value);
    indigo_layout_top_paragraph(c, 18, 110, 0.62f, COL_TEXT_SOFT, 4,
                                indigo_layout_setting_help(row));

    indigo_canvas_text(c, 18, 198, 0.55f, COL_TEXT_DIM,
                       "D-pad or tap a tile; A changes it.");

    /* The build identity, so a bug report can name the build it came from. All
     * three stamped values, because each answers a different question: the tag
     * says which release, the number orders two builds of one tag, and the date
     * says which day. Nothing else in the app prints them, which until now
     * meant the Makefile stamped three strings the linker then dropped. */
    indigo_canvas_text(c, 18, 224, 0.5f, COL_TEXT_DIM, "%s (build %d, %s)",
                       INDIGO_BUILD_COMMIT, INDIGO_BUILD_NUMBER, INDIGO_BUILD_DATE);
}

/* The update screen, top screen: which build this is, and what the updater
 * knows. Every state says in plain words what happens next, and the one that
 * offers an update says what it is checked against and what that does not
 * prove, because "verified" is a word that should not outrun the check. */
static void
build_top_update(const indigo_app *app, indigo_canvas *c)
{
    const indigo_updater *u = &app->updater;
    const char *state;
    uint32_t colour = COL_TEXT_SOFT;
    float y;

    indigo_layout_top_title(c, "Update", "");
    indigo_canvas_text(c, 18, 44, 0.65f, COL_TEXT, "This build: %s",
                       u->current[0] ? u->current : "unknown");
    /* The first line already names the build; this line adds the number and the
     * date, so the commit is not repeated. */
    indigo_canvas_text(c, 18, 62, 0.5f, COL_TEXT_DIM, "build %d, %s", INDIGO_BUILD_NUMBER,
                       INDIGO_BUILD_DATE);

    switch (u->state) {
    case INDIGO_UPDATER_UNAVAILABLE:
        state = u->message;
        break;
    case INDIGO_UPDATER_IDLE:
        state = "I have not asked GitHub yet. Updates come only from the releases of ewanc26/indigo.";
        break;
    case INDIGO_UPDATER_CHECKING:
        state = "Asking GitHub for the latest release...";
        break;
    case INDIGO_UPDATER_UP_TO_DATE:
        state = "You have the latest release.";
        break;
    case INDIGO_UPDATER_AVAILABLE:
        state = "A newer release is available. Nothing has been downloaded.";
        break;
    case INDIGO_UPDATER_DOWNLOADING:
        state = "Downloading, then checking the size and SHA-256 before anything on the card changes...";
        break;
    case INDIGO_UPDATER_READY:
        state = "Downloaded and checked. Putting it in place...";
        break;
    case INDIGO_UPDATER_INSTALLED:
        state = "Done. Press START, then open Indigo again from the Homebrew Menu.";
        break;
    case INDIGO_UPDATER_FAILED:
    default:
        state = u->message;
        colour = COL_ERROR;
        break;
    }
    y = indigo_layout_top_paragraph(c, 18, 90, 0.6f, colour, 4, state);
    if (u->state == INDIGO_UPDATER_AVAILABLE) {
        indigo_canvas_text(c, 18, y + 6, 0.7f, COL_TEXT, "Available: %s (%lu KB)", u->latest,
                           (u->size + 1023) / 1024);
        indigo_layout_top_paragraph(c, 18, y + 30, 0.5f, COL_TEXT_DIM, 4,
                      "The check is a SHA-256 published in the same release. It catches a bad "
                      "download; it does not prove the release is mine, because it is not signed.");
    } else if (u->state == INDIGO_UPDATER_DOWNLOADING || u->state == INDIGO_UPDATER_READY ||
               u->state == INDIGO_UPDATER_INSTALLED) {
        indigo_layout_top_paragraph(c, 18, y + 6, 0.55f, COL_TEXT_DIM, 2,
                      "The old build is kept as indigo-previous.3dsx until the new one starts.");
    }
}

/* The full-size viewer, top screen: the picture and nothing else.
 *
 * The surround is a fixed near-black rather than the theme's background, which
 * is the one place in Indigo that ignores the palette. A photograph is judged
 * against what is around it, and a pale surround puts a box round every image
 * that is lighter than the box; every other screen is showing text and panels,
 * where the theme is the point. */
static void
build_top_image(const indigo_app *app, indigo_canvas *c)
{
    const indigo_image *img = &app->image;
    /* A square is the assumption that distorts least when the server declared
     * no aspect ratio, because the box drawn is stretched to whatever shape it
     * is given -- the same choice the detail band makes. */
    float aw = img->aspect_w && img->aspect_h ? (float) img->aspect_w : 1.0f;
    float ah = img->aspect_w && img->aspect_h ? (float) img->aspect_h : 1.0f;
    float k = INDIGO_TOP_WIDTH / aw;
    float by_height = INDIGO_TOP_HEIGHT / ah;
    float w;
    float h;

    indigo_canvas_rect(c, 0, 0, INDIGO_TOP_WIDTH, INDIGO_TOP_HEIGHT, COL_VIEWER_BG);
    if (!img->url[0]) {
        indigo_canvas_text(c, 18, 104, 0.7f, COL_TEXT_SOFT, "No image to show.");
        return;
    }
    /* The detail band's fit, against the whole screen rather than the band:
     * scale by the smaller of the two ratios, so a 1:3 panorama comes out
     * 400x133 instead of being 1200px tall. */
    if (by_height < k) {
        k = by_height;
    }
    w = aw * k;
    h = ah * k;
    indigo_canvas_image(c, (INDIGO_TOP_WIDTH - w) / 2.0f, (INDIGO_TOP_HEIGHT - h) / 2.0f, w,
                        h, img->url, indigo_media_placeholder_color(img->url));
}

void
indigo_layout_build_top(const indigo_app *app, indigo_canvas *c)
{
    indigo_layout_set_palette(&app->settings);

    indigo_canvas_init(c, INDIGO_TOP_WIDTH, INDIGO_TOP_HEIGHT);
    /* The viewer paints the whole screen itself, in a colour that is not the
     * theme's, so the themed background is not drawn first and then covered. */
    if (app->screen != INDIGO_SCREEN_IMAGE) {
        indigo_canvas_rect(c, 0, 0, INDIGO_TOP_WIDTH, INDIGO_TOP_HEIGHT, COL_BG_TOP);
    }

    if (app->screen == INDIGO_SCREEN_HOME || app->screen == INDIGO_SCREEN_THREAD ||
        app->screen == INDIGO_SCREEN_BOOKMARKS) {
        indigo_canvas_rect(c, 0, 0, INDIGO_TOP_WIDTH, 32, COL_BAR);
        indigo_layout_build_top_post(app, c);
        return;
    }
    switch (app->screen) {
    case INDIGO_SCREEN_PROFILE:
        build_top_profile(app, c);
        return;
    case INDIGO_SCREEN_PROFILE_EDIT:
        build_top_profile_edit(app, c);
        return;
    case INDIGO_SCREEN_NOTIFICATIONS:
        build_top_notifications(app, c);
        return;
    case INDIGO_SCREEN_MESSAGES:
        build_top_messages(app, c);
        return;
    case INDIGO_SCREEN_CHAT_REQUESTS:
        build_top_requests(app, c);
        return;
    case INDIGO_SCREEN_QUEUE:
        build_top_queue(app, c);
        return;
    case INDIGO_SCREEN_DRAFTS:
        build_top_drafts(app, c);
        return;
    case INDIGO_SCREEN_CHAT_THREAD:
        build_top_chat_thread(app, c);
        return;
    case INDIGO_SCREEN_CHAT_JOIN_REQUESTS:
        build_top_chat_join_requests(app, c);
        return;
    case INDIGO_SCREEN_CHAT_GROUP:
        build_top_chat_group(app, c);
        return;
    case INDIGO_SCREEN_MENU:
        build_top_menu(app, c);
        return;
    case INDIGO_SCREEN_COMPOSE:
        build_top_compose(app, c);
        return;
    case INDIGO_SCREEN_SEARCH:
        build_top_search(app, c);
        return;
    case INDIGO_SCREEN_SETTINGS:
        build_top_settings(app, c);
        return;
    case INDIGO_SCREEN_CREDITS:
        build_top_credits(app, c);
        return;
    case INDIGO_SCREEN_IMAGE:
        build_top_image(app, c);
        return;
    case INDIGO_SCREEN_UPDATE:
        build_top_update(app, c);
        return;
    default:
        break;
    }

    indigo_canvas_rect(c, 0, 0, INDIGO_TOP_WIDTH, 46, COL_BAR);
    indigo_canvas_text(c, 18, 10, 1.0f, COL_TEXT, "Indigo");

    indigo_canvas_text(c, 18, 62, 0.7f, COL_TEXT_SOFT,
                       "Native Bluesky client");

    if (app->screen == INDIGO_SCREEN_SIGNIN) {
        build_top_signin(app, c);
        return;
    }
}
