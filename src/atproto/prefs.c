#include "atproto/prefs.h"

#include "util/log.h"

#include <string.h>

void
indigo_prefs_clear(indigo_prefs *prefs)
{
    if (prefs) {
        memset(prefs, 0, sizeof *prefs);
    }
}

void
indigo_prefs_from_wolfram(indigo_prefs *prefs, const wf_actor_preferences *src,
                          long long now)
{
    if (!prefs) {
        return;
    }
    indigo_prefs_clear(prefs);
    if (!src) {
        return;
    }
    for (size_t i = 0; i < src->feed_view_count; i++) {
        const wf_actor_pref_feed_view *fv = &src->feed_views[i];

        if (fv->feed && strcmp(fv->feed, "home") == 0 && fv->has_hide_reposts) {
            prefs->hide_reposts = fv->hide_reposts;
        }
    }
    wf_muted_list_from_prefs(&prefs->muted, src, (int64_t) now);
    indigo_log_info("prefs: %u muted word(s), hide reposts %s", (unsigned) prefs->muted.count,
                    prefs->hide_reposts ? "on" : "off");
}

bool
indigo_prefs_text_is_muted(const indigo_prefs *prefs, const char *text,
                           const char *const *tags, int tag_count)
{
    if (!prefs) {
        return false;
    }
    return wf_muted_list_match(&prefs->muted, text, tags,
                               tags && tag_count > 0 ? (size_t) tag_count : 0, false);
}

bool
indigo_prefs_post_is_hidden(const indigo_prefs *prefs, const indigo_post *post,
                            bool home)
{
    if (!prefs || !post) {
        return false;
    }
    if (home && prefs->hide_reposts && post->reposted_by[0]) {
        return true;
    }
    if (prefs->muted.count == 0) {
        return false;
    }
    const char *tags[INDIGO_POST_FACETS_MAX];
    int tag_count = 0;

    for (unsigned i = 0; i < post->facet_count && i < INDIGO_POST_FACETS_MAX; i++) {
        if (post->facets[i].kind == INDIGO_FACET_TAG) {
            tags[tag_count++] = post->facets[i].target;
        }
    }
    return indigo_prefs_text_is_muted(prefs, post->text, tags, tag_count);
}

unsigned
indigo_prefs_filter_page(const indigo_prefs *prefs, indigo_post *page,
                          unsigned count, unsigned from, bool home)
{
    if (!prefs || !page || from > count) {
        return 0;
    }
    unsigned out = from;

    for (unsigned i = from; i < count; i++) {
        if (indigo_prefs_post_is_hidden(prefs, &page[i], home)) {
            continue;
        }
        if (out != i) {
            page[out] = page[i];
        }
        out++;
    }
    return count - out;
}
