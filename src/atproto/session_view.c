/* Turning Wolfram's views into Indigo's post, actor and notification types. */

#include "atproto/session_internal.h"

#if defined(__3DS__) && defined(WOLFRAM_3DS)

static void
embed_note(const wf_post_display *d, char *dst, size_t cap)
{
    dst[0] = '\0';
    switch (d->embed_kind) {
    case WF_EMBED_IMAGES:
        snprintf(dst, cap, d->image_count == 1 ? "[1 image]" : "[%u images]",
                 (unsigned) d->image_count);
        break;
    case WF_EMBED_VIDEO:
        snprintf(dst, cap, "[video]");
        break;
    case WF_EMBED_EXTERNAL:
        snprintf(dst, cap, "Link: %s", d->external_title && d->external_title[0]
                                           ? d->external_title
                                           : (d->external_uri ? d->external_uri : ""));
        break;
    case WF_EMBED_RECORD:
    case WF_EMBED_RECORD_WITH_MEDIA:
        if (d->quote_uri) {
            snprintf(dst, cap, "Quote @%s: %s",
                     d->quote_author_handle ? d->quote_author_handle : "?",
                     d->quote_text ? d->quote_text : "");
        } else {
            snprintf(dst, cap, "[quoted post unavailable]");
        }
        break;
    case WF_EMBED_UNKNOWN:
        snprintf(dst, cap, "[attachment]");
        break;
    case WF_EMBED_NONE:
        break;
    }
    for (char *c = dst; *c; c++) {
        if (*c == '\n' || *c == '\r') {
            *c = ' ';
        }
    }
}

unsigned
indigo_session_count_of(int v)
{
    return v > 0 ? (unsigned) v : 0;
}

/* Carry the embed onto the post, for the screens that draw it. The
 * display helper owns the safe text/facet view; the typed embed reader supplies
 * image metadata that the display summary deliberately keeps bounded to a count. */
static void
fill_embed(const wf_post_display *d, const cJSON *raw_embed, indigo_post *out)
{
    wf_post_embed embed = {0};

    out->embed_count =
        d->image_count > INDIGO_EMBED_IMAGES_MAX ? INDIGO_EMBED_IMAGES_MAX
                                                  : (unsigned char) d->image_count;

    if (wf_post_embed_from_json(raw_embed, &embed) != WF_OK) {
        return;
    }

    if (embed.image_count > 0 && embed.images) {
        for (size_t i = 0; i < embed.image_count; i++) {
            const wf_post_embed_image *img = &embed.images[i];
            if (!img->thumb || !img->thumb[0]) {
                continue;
            }
            if (out->embed_kind == INDIGO_EMBED_NONE) {
                out->embed_kind = INDIGO_EMBED_IMAGE;
                indigo_media_cdn_url(out->embed_thumb, sizeof out->embed_thumb, img->thumb, INDIGO_CDN_THUMBNAIL);
                indigo_copy_utf8(out->embed_alt, sizeof out->embed_alt,
                                 img->alt ? img->alt : "");
                if (img->width > 0 && img->height > 0) {
                    unsigned w = (unsigned) img->width;
                    unsigned h = (unsigned) img->height;
                    while (w > 255 || h > 255) {
                        w /= 2;
                        h /= 2;
                    }
                    out->embed_w = (unsigned char) (w ? w : 1);
                    out->embed_h = (unsigned char) (h ? h : 1);
                }
                break;
            }
        }
    }

    if (out->embed_kind == INDIGO_EMBED_NONE && embed.has_external &&
        embed.external_uri) {
        out->embed_kind = INDIGO_EMBED_LINK;
        indigo_copy_utf8(out->embed_title, sizeof out->embed_title,
                         embed.external_title ? embed.external_title : "");
        indigo_copy_utf8(out->embed_uri, sizeof out->embed_uri, embed.external_uri);
        indigo_media_cdn_url(out->embed_thumb, sizeof out->embed_thumb, embed.external_thumb,
                             INDIGO_CDN_THUMBNAIL);
    }

    wf_post_embed_free(&embed);
}

/* False when the post cannot be shown at all (no URI). */
bool
indigo_session_fill_post(const wf_agent_post_view *pv, indigo_post *out)
{
    wf_post_display d;

    memset(out, 0, sizeof *out);
    if (!pv->uri || !pv->cid || strlen(pv->uri) >= sizeof out->uri ||
        strlen(pv->cid) >= sizeof out->cid) {
        return false;
    }
    snprintf(out->uri, sizeof out->uri, "%s", pv->uri);
    snprintf(out->cid, sizeof out->cid, "%s", pv->cid);
    indigo_copy_utf8(out->handle, sizeof out->handle, pv->author.handle);
    indigo_copy_utf8(out->display_name, sizeof out->display_name, pv->author.display_name);
    indigo_media_cdn_url(out->avatar, sizeof out->avatar, pv->author.avatar, INDIGO_CDN_AVATAR);
    out->like_count = indigo_session_count_of(pv->like_count);
    out->repost_count = indigo_session_count_of(pv->repost_count);
    out->reply_count = indigo_session_count_of(pv->reply_count);
    if (pv->viewer.like && strlen(pv->viewer.like) < sizeof out->like_uri) {
        snprintf(out->like_uri, sizeof out->like_uri, "%s", pv->viewer.like);
    }
    if (pv->viewer.repost && strlen(pv->viewer.repost) < sizeof out->repost_uri) {
        snprintf(out->repost_uri, sizeof out->repost_uri, "%s", pv->viewer.repost);
    }

    if (wf_agent_post_view_display(pv, &d) == WF_OK) {
        indigo_copy_utf8(out->text, sizeof out->text, d.text);
        out->is_reply = d.is_reply != 0;
        {
            char note[256];

            embed_note(&d, note, sizeof note);
            indigo_copy_utf8(out->embed_note, sizeof out->embed_note, note);
        }
        fill_embed(&d, pv->embed, out);
        for (size_t i = 0; i < d.facet_count && out->facet_count < INDIGO_POST_FACETS_MAX; i++) {
            const wf_display_facet *f = &d.facets[i];

            if (f->byte_end > strlen(out->text)) {
                break; /* the text was truncated before this facet */
            }
            out->facets[out->facet_count].kind =
                f->kind == WF_FACET_MENTION ? INDIGO_FACET_MENTION
                : f->kind == WF_FACET_TAG   ? INDIGO_FACET_TAG
                                            : INDIGO_FACET_LINK;
            out->facets[out->facet_count].start = (unsigned) f->byte_start;
            out->facets[out->facet_count].end = (unsigned) f->byte_end;
            indigo_copy_utf8(out->facets[out->facet_count].target,
                             sizeof out->facets[out->facet_count].target,
                             f->target ? f->target : "");
            out->facet_count++;
        }
        wf_post_display_free(&d);
    }
    return true;
}

bool
indigo_session_to_post(const wf_agent_feed_item *item, indigo_post *out)
{
    char *by = NULL;

    if (!indigo_session_fill_post(&item->post, out)) {
        return false;
    }
    if (wf_agent_feed_item_reposted_by(item, &by) == WF_OK && by) {
        indigo_copy_utf8(out->reposted_by, sizeof out->reposted_by, by);
        free(by);
    }
    return true;
}

/* A thread post is a postView without the bookmark fields: borrow its
 * pointers (nothing here is freed) and reuse the one display path. */
bool
indigo_session_thread_post_to_post(const wf_agent_thread_post *tp, unsigned depth, indigo_post *out)
{
    wf_agent_post_view pv;

    memset(&pv, 0, sizeof pv);
    pv.uri = tp->uri;
    pv.cid = tp->cid;
    pv.author = tp->author;
    pv.record = tp->record;
    pv.embed = tp->embed;
    pv.reply_count = tp->reply_count;
    pv.repost_count = tp->repost_count;
    pv.like_count = tp->like_count;
    pv.quote_count = tp->quote_count;
    pv.indexed_at = tp->indexed_at;
    pv.viewer.like = tp->viewer_like;
    pv.viewer.repost = tp->viewer_repost;
    if (!indigo_session_fill_post(&pv, out)) {
        return false;
    }
    out->depth = (unsigned char) (depth > 6 ? 6 : depth);
    return true;
}

void
indigo_session_add_replies(const wf_agent_thread_node *n, unsigned depth)
{
    for (size_t i = 0; i < n->replies_count && g_session.page_count < INDIGO_THREAD_MAX; i++) {
        const wf_agent_thread_node *r = &n->replies[i];

        if (r->kind != WF_AGENT_THREAD_KIND_POST) {
            continue;
        }
        if (indigo_session_thread_post_to_post(&r->post, depth, &g_session.page[g_session.page_count])) {
            g_session.page_count++;
            indigo_session_add_replies(r, depth + 1);
        }
    }
}

/* Every list of people -- search, followers, following, list members, mutes,
 * blocks -- arrives as the same Wolfram actor view, so they all fill the same
 * row through this one function rather than six copies of it. The avatar URL
 * rides along because the row has somewhere to show it. */
bool
indigo_session_fill_actor(const wf_agent_profile_view *a, indigo_actor *o)
{
    if (!a->handle || !a->handle[0]) {
        /* A profile with no handle cannot be opened, and a row that does
         * nothing is worse than a missing one. */
        return false;
    }
    memset(o, 0, sizeof *o);
    indigo_copy_utf8(o->handle, sizeof o->handle, a->handle);
    indigo_copy_utf8(o->display_name, sizeof o->display_name,
                     a->display_name ? a->display_name : "");
    indigo_copy_utf8(o->did, sizeof o->did, a->did ? a->did : "");
    indigo_media_cdn_url(o->avatar, sizeof o->avatar, a->avatar, INDIGO_CDN_AVATAR);
    return true;
}

indigo_note_kind
indigo_session_note_kind(const char *reason)
{
    if (!reason) {
        return INDIGO_NOTE_OTHER;
    }
    if (strcmp(reason, "like") == 0) {
        return INDIGO_NOTE_LIKE;
    }
    if (strcmp(reason, "repost") == 0) {
        return INDIGO_NOTE_REPOST;
    }
    if (strcmp(reason, "follow") == 0) {
        return INDIGO_NOTE_FOLLOW;
    }
    if (strcmp(reason, "reply") == 0) {
        return INDIGO_NOTE_REPLY;
    }
    if (strcmp(reason, "mention") == 0) {
        return INDIGO_NOTE_MENTION;
    }
    if (strcmp(reason, "quote") == 0) {
        return INDIGO_NOTE_QUOTE;
    }
    return INDIGO_NOTE_OTHER;
}

#endif /* 3DS with Wolfram */
