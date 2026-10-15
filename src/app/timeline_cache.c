#include "app/timeline_cache.h"

#include <string.h>

void
indigo_timeline_cache_capture(const indigo_timeline *t, indigo_cache_timeline *out)
{
    unsigned n = t->count < INDIGO_CACHE_POSTS_MAX ? t->count : INDIGO_CACHE_POSTS_MAX;

    indigo_cache_timeline_clear(out);
    for (unsigned i = 0; i < n; i++) {
        const indigo_post *p = &t->posts[i];
        indigo_cache_post *c = &out->posts[out->count];

        if (p->uri[0] == '\0') {
            continue;
        }
        indigo_copy_utf8(c->uri, sizeof c->uri, p->uri);
        indigo_copy_utf8(c->cid, sizeof c->cid, p->cid);
        indigo_copy_utf8(c->handle, sizeof c->handle, p->handle);
        indigo_copy_utf8(c->display_name, sizeof c->display_name, p->display_name);
        indigo_copy_utf8(c->text, sizeof c->text, p->text);
        out->count++;
    }
}

unsigned
indigo_timeline_cache_restore(indigo_timeline *t, const indigo_cache_timeline *in)
{
    unsigned restored = 0;

    for (size_t i = 0; i < in->count; i++) {
        const indigo_cache_post *c = &in->posts[i];
        indigo_post p;

        memset(&p, 0, sizeof p);
        indigo_copy_utf8(p.uri, sizeof p.uri, c->uri);
        indigo_copy_utf8(p.cid, sizeof p.cid, c->cid);
        indigo_copy_utf8(p.handle, sizeof p.handle, c->handle);
        indigo_copy_utf8(p.display_name, sizeof p.display_name, c->display_name);
        indigo_copy_utf8(p.text, sizeof p.text, c->text);
        if (!indigo_timeline_append(t, &p)) {
            break;
        }
        restored++;
    }
    return restored;
}
