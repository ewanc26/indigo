/* The Wolfram half of the preferences: turning wf_actor_preferences into the
 * pure indigo_prefs. Separate from prefs.c because that file must build on
 * the host for `make test`, and this one cannot -- it needs Wolfram's 3DS
 * build. The session worker is its only caller. */

#include "atproto/prefs.h"
#include "util/log.h"

#include <wolfram/actor_prefs_typed.h>

#include <string.h>

void
indigo_prefs_from_wolfram(indigo_prefs *prefs,
                           const wf_actor_preferences *src,
                           long long now)
{
    if (!prefs) {
        return;
    }
    indigo_prefs_clear(prefs);
    prefs->now = now;
    if (!src) {
        return;
    }

    for (size_t i = 0; i < src->feed_view_count; i++) {
        const wf_actor_pref_feed_view *fv = &src->feed_views[i];

        if (fv->feed && strcmp(fv->feed, "home") == 0 && fv->has_hide_reposts) {
            prefs->hide_reposts = fv->hide_reposts;
        }
    }

    for (size_t i = 0; i < src->muting_keyword_count; i++) {
        const wf_actor_pref_muted_word *mw = &src->muting_keywords[i];

        if (!mw->value) {
            continue;
        }
        bool content = false;
        bool tag = false;

        for (size_t t = 0; t < mw->target_count; t++) {
            if (!mw->targets[t]) {
                continue;
            }
            content = content || strcmp(mw->targets[t], "content") == 0;
            tag = tag || strcmp(mw->targets[t], "tag") == 0;
        }
        indigo_prefs_add_word(prefs, mw->value, content, tag, mw->expires_at);
    }
    indigo_log_info("prefs: %u muted word(s), hide reposts %s", prefs->count,
                    prefs->hide_reposts ? "on" : "off");
}
