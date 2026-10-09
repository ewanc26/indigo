#ifndef INDIGO_SOCIAL_H
#define INDIGO_SOCIAL_H

#include "app/timeline.h"

#include <stdbool.h>
#include <wolfram/qr.h>

#define INDIGO_NOTIFICATION_MAX 30
#define INDIGO_PROFILE_BIO_MAX 320
#define INDIGO_DRAFT_MAX 1024
/* did:plc: identifiers are short, but a custom-domain did is whatever the
 * domain's PLC record allows, so leave room rather than truncating one.
 * Matches INDIGO_SEARCH_DID_MAX; the profile needs its own copy because
 * social.h must not depend on search.h. */
#define INDIGO_PROFILE_DID_MAX 128

typedef enum {
    INDIGO_NOTE_OTHER = 0,
    INDIGO_NOTE_LIKE,
    INDIGO_NOTE_REPOST,
    INDIGO_NOTE_FOLLOW,
    INDIGO_NOTE_REPLY,
    INDIGO_NOTE_MENTION,
    INDIGO_NOTE_QUOTE,
} indigo_note_kind;

typedef struct {
    indigo_note_kind kind;
    char name[INDIGO_POST_NAME_MAX];
    char handle[INDIGO_POST_NAME_MAX];
    char avatar[INDIGO_MEDIA_URL_MAX];
    /* Text of the reply/mention/quote; empty for likes, reposts, follows. */
    char text[INDIGO_POST_NOTE_MAX * 2];
    /* The post to open: the notification's own post for reply/mention/quote,
     * the person's own post for like/repost. Empty for follows. */
    char target_uri[INDIGO_POST_URI_MAX];
    bool unread;
} indigo_notification;

typedef struct {
    indigo_notification items[INDIGO_NOTIFICATION_MAX];
    unsigned count;
    unsigned selected;
    unsigned scroll;
    bool loading;
    char status[INDIGO_POST_NOTE_MAX];
    bool status_is_error;
} indigo_notifications;

typedef struct {
    char handle[INDIGO_POST_NAME_MAX];
    char display_name[INDIGO_POST_NAME_MAX];
    char avatar[INDIGO_MEDIA_URL_MAX];
    char bio[INDIGO_PROFILE_BIO_MAX];
    unsigned followers;
    unsigned follows;
    unsigned posts;
    bool following;
    /* Wolfram's getProfile gives both of these, and neither can be recovered
     * from the fields above: follow needs the subject's did, and unfollow is
     * by record URI with no handle to resolve it from. */
    char did[INDIGO_PROFILE_DID_MAX];
    char follow_uri[INDIGO_POST_URI_MAX];
    /* Mute is an account-level flag with no record URI; block is a repo record
     * deleted by URI, like follow. Both come from the same getProfile
     * response, so keeping them costs nothing once did and follow_uri are
     * here. */
    bool muted;
    bool blocked;
    char block_uri[INDIGO_POST_URI_MAX];
    /* pinnedPost.uri, or empty. getProfile already returns it, so keeping it
     * costs nothing; opening it needs no extra request, only the URI. */
    char pinned_uri[INDIGO_POST_URI_MAX];
    /* Set while a follow/unfollow is in flight. The `following` flag flips
     * immediately as an optimistic update and is put back if the job fails. */
    bool follow_busy;
    /* The same for mute and block. */
    bool mute_busy;
    bool block_busy;
    bool loaded;
    bool loading;
    char status[INDIGO_POST_NOTE_MAX];
    bool status_is_error;
} indigo_profile;

/* Mute and block are split from follow because their record shapes differ:
 * mute is an account-level flag with no URI at all, block is a repo record
 * deleted by URI, and follow is a third shape again. The enum lives here, not
 * in the session layer, because it names what the person asked for rather
 * than which Wolfram call runs; session.h includes this header. */
typedef enum {
    INDIGO_GRAPH_NONE = 0,
    INDIGO_GRAPH_MUTE,
    INDIGO_GRAPH_UNMUTE,
    INDIGO_GRAPH_BLOCK,
    INDIGO_GRAPH_UNBLOCK,
} indigo_graph_action;

typedef enum {
    INDIGO_COMPOSE_POST = 0,
    INDIGO_COMPOSE_REPLY,
    INDIGO_COMPOSE_QUOTE,
} indigo_compose_mode;

/* Who may reply to a new top-level post. Not offered on replies: Bluesky
 * scopes a threadgate to the post it is attached to, and gating a reply's own
 * replies apart from the rest of the thread is confusing enough that the
 * official client does not offer it either. */
typedef enum {
    INDIGO_REPLY_GATE_EVERYONE = 0,
    INDIGO_REPLY_GATE_FOLLOWED_MENTIONED,
    INDIGO_REPLY_GATE_NOBODY,
    INDIGO_REPLY_GATE_COUNT,
} indigo_reply_gate;

#define INDIGO_IMAGE_PATH_MAX 192
#define INDIGO_IMAGE_ALT_MAX 256
/* Where images to attach are looked for. The person copies .jpg and .png files
 * here from a computer. */
#define INDIGO_IMAGES_DIR "sdmc:/3ds/indigo/images"
/* The 3DS camera's folder. It saves photos as DCIM/<folder>/<image>, so the
 * picker also lists the images one folder down here. */
#define INDIGO_CAMERA_DIR "sdmc:/DCIM"

/* A post being written. Reply and quote both refer to `target`; a reply also
 * needs the thread root so it lands in the right conversation. */
typedef struct {
    indigo_compose_mode mode;
    indigo_reply_gate reply_gate;
    char text[INDIGO_DRAFT_MAX];
    /* One image to attach, by path, with its alt text; empty for none. */
    char image[INDIGO_IMAGE_PATH_MAX];
    char image_alt[INDIGO_IMAGE_ALT_MAX];
    indigo_post target;
    char root_uri[INDIGO_POST_URI_MAX];
    char root_cid[INDIGO_POST_CID_MAX];
    bool has_target;
    bool sending;
    char status[INDIGO_POST_NOTE_MAX];
    bool status_is_error;
} indigo_compose;

/* The full-size image viewer. It is a copy rather than a pointer into a post,
 * because the post it came from can be paged away underneath it -- the thread
 * list reloads, the timeline keeps fetching -- and a viewer pointing at a
 * recycled post would change pictures while someone is looking at one. */
typedef struct {
    /* The CDN thumbnail URL, which is what the post carries. Bluesky serves
     * that at up to 640px, so on a 400px screen it is the image at its own
     * resolution rather than an enlargement; the original blob is only in the
     * view record's fullsize variant, which Indigo does not ask for. */
    char url[INDIGO_EMBED_URL_MAX];
    char alt[INDIGO_EMBED_ALT_MAX];
    /* The server's declared aspect ratio, 0/0 when it declared none. Drawn
     * from rather than measured, so the box is right before anything is
     * decoded and no layout depends on a fetch having finished. */
    unsigned aspect_w;
    unsigned aspect_h;
    /* How many images the author attached. One is shown, because one is what
     * the post carries; the count is what says so rather than implying this is
     * all of them. */
    unsigned count;
} indigo_image;

/* True when the post has an image the viewer can open. A link card, a quote, a
 * video and a post with no embed all cannot, which is why this is a question
 * about the post rather than a flag the viewer keeps. */
bool indigo_post_has_image(const indigo_post *p);

void indigo_notifications_clear(indigo_notifications *n);
bool indigo_notifications_move(indigo_notifications *n, int delta, unsigned rows);
bool indigo_notifications_select(indigo_notifications *n, unsigned index, unsigned rows);
const indigo_notification *indigo_notifications_selected(const indigo_notifications *n);

/* The More menu. It is built from the post being read, so its facet targets
 * come first, then the app-wide actions. The cap has to hold every app-wide
 * action plus a post's worth of facet targets, or add_item silently drops the
 * last entries and "Close menu" with them. */
#define INDIGO_MENU_MAX 24
#define INDIGO_MENU_ROWS 5

typedef enum {
    INDIGO_MENU_COMPOSE = 0,
    INDIGO_MENU_NOTIFICATIONS,
    INDIGO_MENU_FIND_PEOPLE,
    INDIGO_MENU_FIND_POSTS,
    INDIGO_MENU_LISTS,
    INDIGO_MENU_FEEDS,
    INDIGO_MENU_MUTED,
    INDIGO_MENU_BLOCKED,
    INDIGO_MENU_MY_PROFILE,
    INDIGO_MENU_SETTINGS,
    INDIGO_MENU_UPDATE,
    INDIGO_MENU_SIGN_OUT,
    INDIGO_MENU_LIKED_BY,
    INDIGO_MENU_REPOSTED_BY,
    INDIGO_MENU_OPEN_MENTION,
    INDIGO_MENU_SHOW_TAG,
    INDIGO_MENU_SHOW_LINK,
    INDIGO_MENU_PICK_IMAGE,
    INDIGO_MENU_CLOSE,
} indigo_menu_kind;

typedef struct {
    indigo_menu_kind kind;
    char label[INDIGO_POST_NAME_MAX * 2];
    char payload[INDIGO_FACET_TARGET_MAX];
    /* For an image picker row: the payload is under the camera folder rather
     * than the images folder. */
    bool camera;
} indigo_menu_item;

typedef struct {
    /* "Menu", or "Images" while it is the picker for an attachment. */
    char title[INDIGO_POST_NAME_MAX];
    /* True while the menu is the attachment picker, so the top screen can say
     * where the pictures come from. */
    bool picking_image;
    bool showing_link;
    char link_url[INDIGO_FACET_TARGET_MAX];
    int qr_size;
    uint8_t qr[WF_QR_MAX_SIZE * WF_QR_MAX_SIZE];
    indigo_menu_item items[INDIGO_MENU_MAX];
    unsigned count;
    unsigned selected;
    unsigned scroll;
    /* The post the menu was opened on, for the entries that are about it. */
    char post_uri[INDIGO_POST_URI_MAX];
} indigo_menu;

/* `post` may be NULL when nothing is selected: the facet targets are then
 * left out. `account` is the signed-in handle for the profile entry. */
void indigo_menu_build(indigo_menu *m, const indigo_post *post, const char *account);
/* Fill the menu with the postable images in `images_dir` (see wolfram/attach.h)
 * and, one folder down, in `camera_dir`. Each file is an INDIGO_MENU_PICK_IMAGE
 * item whose payload is its name under that folder ("folder/name" for the
 * camera's subfolders), with `camera` set for the camera rows. A Close item
 * follows, so an empty picker still has a way out. `title` becomes "Images". */
void indigo_menu_build_images(indigo_menu *m, const char *images_dir, const char *camera_dir);
/* Open the selected web link in the menu's QR view; false means the URL could
 * not be encoded, but the URL is still retained for display. */
bool indigo_menu_show_link(indigo_menu *m, const char *url);
/* Move the selection, keeping it inside the `rows` visible rows. */
bool indigo_menu_move(indigo_menu *m, int delta, unsigned rows);
/* The item shown in visible row `row`, or NULL when that row is empty. */
const indigo_menu_item *indigo_menu_row(const indigo_menu *m, unsigned row, unsigned rows);

/* Compose is allowed to publish when it has text and is not already sending. */
bool indigo_compose_ready(const indigo_compose *c);
bool indigo_compose_has_image(const indigo_compose *c);
void indigo_compose_clear_image(indigo_compose *c);
/* The file name of the attached image, without its folder. */
const char *indigo_compose_image_name(const indigo_compose *c);
/* Quote is only offered when there is a post to quote. */
bool indigo_compose_can_toggle(const indigo_compose *c);
void indigo_compose_toggle(indigo_compose *c);
/* A reply gate is only offered on a new top-level post. This says which
 * control compose is showing; indigo_compose_gate_cycle is what declines to
 * move it mid-send. */
bool indigo_compose_can_gate(const indigo_compose *c);
void indigo_compose_gate_cycle(indigo_compose *c);
/* Spelled out for the top screen, e.g. "People you follow and mention". */
const char *indigo_compose_gate_label(const indigo_compose *c);
/* Compressed to fit the compose pill, e.g. "Follows + mentions". */
const char *indigo_compose_gate_short(const indigo_compose *c);

#endif
