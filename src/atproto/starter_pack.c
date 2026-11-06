#include "atproto/starter_pack.h"

#include <cJSON.h>
#include <wolfram/graph_social_typed.h>

#include <stdio.h>
#include <string.h>

void
indigo_starter_pack_reset(indigo_starter_pack *pack)
{
    if (pack) {
        memset(pack, 0, sizeof *pack);
    }
}

bool
indigo_starter_pack_uri_from_link(const char *link, char *out, size_t out_size)
{
    static const char AT_PREFIX[] = "at://";
    static const char COLLECTION[] = "app.bsky.graph.starterpack";
    static const char HTTPS_PREFIX[] = "https://bsky.app/starter-pack/";
    static const char HTTP_PREFIX[] = "http://bsky.app/starter-pack/";
    const char *authority;
    const char *collection;
    const char *rkey;
    const char *path;
    const char *authority_end;
    const char *rkey_end;
    size_t len;
    int written;

    if (!out || out_size == 0) {
        return false;
    }
    out[0] = '\0';
    if (!link) {
        return false;
    }

    if (strncmp(link, AT_PREFIX, sizeof AT_PREFIX - 1) == 0) {
        authority = link + sizeof AT_PREFIX - 1;
        collection = strchr(authority, '/');
        if (!collection || collection == authority) {
            return false;
        }
        collection++;
        if (strncmp(collection, COLLECTION, sizeof COLLECTION - 1) != 0 ||
            collection[sizeof COLLECTION - 1] != '/') {
            return false;
        }
        rkey = collection + sizeof COLLECTION;
        if (!rkey[0] || strchr(rkey, '/')) {
            return false;
        }
        len = strlen(link);
        if (len >= out_size) {
            return false;
        }
        memcpy(out, link, len + 1);
        return true;
    }

    if (strncmp(link, HTTPS_PREFIX, sizeof HTTPS_PREFIX - 1) == 0) {
        path = link + sizeof HTTPS_PREFIX - 1;
    } else if (strncmp(link, HTTP_PREFIX, sizeof HTTP_PREFIX - 1) == 0) {
        path = link + sizeof HTTP_PREFIX - 1;
    } else {
        return false;
    }

    authority_end = strchr(path, '/');
    if (!authority_end || authority_end == path) {
        return false;
    }
    rkey_end = strpbrk(authority_end + 1, "?#");
    if (!rkey_end) {
        rkey_end = path + strlen(path);
    }
    if (rkey_end <= authority_end + 1 || strchr(authority_end + 1, '/')) {
        return false;
    }
    written = snprintf(out, out_size, "at://%.*s/app.bsky.graph.starterpack/%.*s",
                       (int) (authority_end - path), path,
                       (int) (rkey_end - (authority_end + 1)), authority_end + 1);
    return written > 0 && (size_t) written < out_size;
}

int
indigo_starter_pack_fill_from_wolfram(
    indigo_starter_pack *pack, const struct wf_graph_starter_pack_view *src)
{
    const wf_graph_starter_pack_view *view = src;
    const cJSON *record;
    const cJSON *list;
    const cJSON *feeds;
    const cJSON *feed;
    int added = 0;

    if (!pack || !view) {
        return 0;
    }
    indigo_starter_pack_reset(pack);
    indigo_copy_utf8(pack->uri, sizeof pack->uri, view->uri ? view->uri : "");
    indigo_copy_utf8(pack->name, sizeof pack->name,
                     view->name && view->name[0] ? view->name : "Starter pack");
    indigo_copy_utf8(pack->creator_handle, sizeof pack->creator_handle,
                     view->creator.handle ? view->creator.handle : "");
    indigo_copy_utf8(pack->creator_name, sizeof pack->creator_name,
                     view->creator.display_name && view->creator.display_name[0]
                         ? view->creator.display_name
                         : (view->creator.handle ? view->creator.handle : ""));
    indigo_copy_utf8(pack->creator_avatar, sizeof pack->creator_avatar,
                     view->creator.avatar ? view->creator.avatar : "");
    indigo_copy_utf8(pack->description, sizeof pack->description,
                     view->description ? view->description : "");
    if (view->has_list_item_count && view->list_item_count > 0) {
        pack->member_count = view->list_item_count > 1000000
                                 ? 1000000
                                 : (unsigned) view->list_item_count;
    }

    record = view->record;
    if (!record) {
        return 0;
    }
    list = cJSON_GetObjectItemCaseSensitive(record, "list");
    if (cJSON_IsString(list) && list->valuestring) {
        indigo_copy_utf8(pack->list_uri, sizeof pack->list_uri, list->valuestring);
    }
    feeds = cJSON_GetObjectItemCaseSensitive(record, "feeds");
    if (!cJSON_IsArray(feeds)) {
        return 0;
    }
    cJSON_ArrayForEach(feed, feeds) {
        const cJSON *uri = cJSON_GetObjectItemCaseSensitive(feed, "uri");

        if (pack->feed_count >= INDIGO_STARTER_PACK_FEEDS_MAX) {
            break;
        }
        if (cJSON_IsString(uri) && uri->valuestring && uri->valuestring[0]) {
            indigo_copy_utf8(pack->feeds[pack->feed_count],
                             sizeof pack->feeds[0], uri->valuestring);
            pack->feed_count++;
            added++;
        }
    }
    return added;
}
