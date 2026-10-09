# Parity

Indigo's column of the parity matrix, written from the code. Cobalt's README "Implemented" list is the reference for the feature rows; Platinum is a different shape (a bridge plus a C89 client) and is compared where the rows overlap.

Each row says one of four things:

- **implemented**: the code is on `main`. The verification column says where it has been run. "Host" means the unit tests and the snapshot renderer; it is not an emulator and not hardware.
- **issue**: a gap, with its issue.
- **not planned**: a decision, with the reason.
- **hardware-impossible**: the console cannot do it. Nothing here is in that state on evidence I have; the rows that look like it are filed as not planned instead.

Nothing in Indigo has been run on real 3DS hardware.

## Sign-in

App password and OAuth are two flows with different dependencies, so they are two rows.

| Flow | State | Verification | Needs |
|---|---|---|---|
| App password | implemented: handle, app password and a service URL (default `https://bsky.social`); follows the account's own PDS | emulator-verified in Azahar against bsky.social and a custom PDS (see README) | a Bluesky app password |
| OAuth through a hosted node | implemented: a handle with an empty password runs Wolfram's pairing driver (`wf_oauth_pair_run`), which shows a pairing URL and code, polls the node and hands back a node bearer token | the pairing contract is tested in Wolfram against shared vectors, on the host. Indigo's use of it is not verified on an emulator or hardware | a Wolfram OAuth node (its `oauth-node.md` page) whose URL goes in the service field. bsky.social is not a node, so the default service URL cannot do this |

OAuth here is not the AT Protocol browser flow running on the console. The node holds the OAuth session and DPoP key; the console holds a bearer token for the node. A node outage ends the session, and the account's PDS never sees Indigo directly.

The pairing contract (field names, terminal and transient failures, test vectors, no tokens in logs) is Wolfram's, from wolfram#101, and Indigo no longer carries a copy of it. `scripts/check-flow.sh protocol` now allows no raw protocol strings in `src/` at all.

An unknown pairing code (a restarted node forgets its pairings) used to be polled for nine minutes. Wolfram's driver treats the 404 as final, so sign-in now fails straight away and says so.

## Features

| Feature | Cobalt | Indigo | Verification and notes |
|---|---|---|---|
| Diagnostics | yes | implemented: a diagnostics setting gates `sdmc:/3ds/indigo/indigo.log`; the settings screen names the build | host; log never holds tokens |
| Persistent session, sign-out | encrypted | implemented, plaintext on the SD card by decision (AGENTS.md section 12) | emulator: save, resume, sign-out |
| Home timeline, paging, reposts, threads | yes | implemented | emulator against a live account |
| Compose, reply, quote | yes | implemented | emulator: modes 0, 1 and 2 published |
| Reply gates | yes | implemented (`src/app/social.c`: everyone, following and mentioned, nobody) | host only |
| Like and repost, with undo | yes | implemented | emulator, counts moved and were undone |
| Delete your own post | yes | implemented | not started | Indigo: confirmation in the thread view; the worker verifies the post AT-URI belongs to the signed-in DID before calling Wolfram. Host tests and 3DS build pass; hardware behaviour is unverified. |
| Direct messages | issue cobalt#107 | issue #22 | not started | Follow Cobalt's design first; read-only inbox and replying are the proposed first steps. |
| Post to a thread (several posts at once) | yes | implemented | not started | Indigo: Add to thread builds up to eight top-level text posts, sent through Wolfram's wf_agent_post_thread. Text-only; partial publication is reported and the draft is cleared to prevent duplicates. Host-tested; hardware behaviour is unverified. |
| Video poster and external-media embeds | partial, cobalt#102 | partial, issue #22 | not started | Indigo already draws a video poster with a cannot-play label (host test: test_layout_draws_video_poster); video duration metadata and other external-media variants remain open. |
| Open a link on a phone via QR code | yes | implemented | not started | Indigo: the More menu's link page encodes with Wolfram's wf_qr_encode, displays the QR matrix on the top screen and the address below; host-tested, not scanned from a physical screen. |
| Notifications, mark as seen | yes | implemented | emulator fetch; seen marking host only |
| Profiles, follow, unfollow | yes | implemented | host |
| Followers, following | yes | implemented, paged | host |
| Profile tabs | yes | yes: posts, replies, media and likes (likes on the signed-in account only), cycled by tapping the header box on a person's posts (`INDIGO_SEARCH_AUTHOR`) | host tests; not run on a 3DS or Azahar |
| Pinned posts | yes | implemented | host |
| Avatars | yes | implemented | host, and Azahar: fetched as 128 px JPEG thumbnails, decoded and drawn on the timeline; not on hardware |
| Images on posts, replies and quotes, with alt text | yes | implemented for viewing, behind an alt-text setting | host only |
| Full-size image viewer | yes | implemented | host only |
| Who liked or reposted a post | yes | implemented (`INDIGO_SEARCH_LIKED_BY`, `INDIGO_SEARCH_REPOSTED_BY`; post menu) | host and 3DS build in CI; #15 closed as already done |
| Link-card previews | yes | implemented | host only |
| Actor search, post search | yes | implemented, paged | host |
| Custom feeds | yes | implemented: the account's saved feeds, one feed on Home | host |
| Lists and members | read-only | implemented, read-only | host |
| Mute and block, with lists | yes | implemented | host |
| Muted words, hide reposts | yes | implemented: Wolfram's matcher (`wf_muted_words_match`), including mute expiry | host |
| Attach an image when composing | yes | implemented: SELECT or the "Add an image" button in compose opens a picker over `sdmc:/3ds/indigo/images` and one folder down in the camera's `sdmc:/DCIM` (JPEG and PNG under 950 KB), then asks for alt text; works on posts, replies and quotes. The type and size filter, the folder scans and the upload are Wolfram's (`wolfram/attach.h`); the camera folder's scan is `wf_attach_scan_images_tree` (Wolfram v0.39.0) | host only: the compose state, the picker over both folders and the layouts (snapshots `compose-image`, `attach-picker`). The upload and a real post have not been run on an emulator or a 3DS, and the DCIM listing has not been seen on a console (#19 stays open for that) |
| On-card cache of timeline and profile data | not listed | issue: #13 | not started |
| Settings screen | n/a | implemented: theme, text scale, alt text, diagnostics, startup feed; reduce motion and high contrast are stored and reserved | host |
| Video, GIFs and animated media | not planned | partial: a video shows its poster frame, decoded at the draw size (400 px cap), with a line saying it cannot play on the 3DS (#69). GIFs are not drawn. | Wolfram's decoder (`wf_image_decode_rgba`) is for still images, and nothing in the 1.5MB image budget accounts for frames. No decoder evaluation has been done, so playback is a decision and not a hardware limit. |
| Auto-update | in progress, to the same wolfram#106 contract | implemented: More > "Check for updates" asks GitHub for the latest release, shows the version and asks before downloading. The download is checked (size and SHA-256), written, re-read from the card, and swapped in with the old build kept until the new one starts. Manifest signed (Ed25519, `update.json.sig`) and refused if unsigned | host only (the screen's states, the pinned URLs, and the swap and its recovery with power lost at every write; docs/UPDATE.md). The network, the card and the swap have not been run on an emulator or hardware (#25) |
| Push notifications | not planned | not planned: no push service a homebrew application can register with; notifications are fetched when the screen is opened | |

## Shared logic

Muted-word matching and the list (`wf_muted_list`), relative time and RFC 3339 (`wolfram/time.h`), failure kinds (`wolfram/failure.h`), CDN image URLs, the drag gesture, the update signature check and the OAuth pairing client are all Wolfram's; Indigo keeps only the wording, the clock and the hooks. The guard is `scripts/check-flow.sh protocol` plus the duplication scan described in AGENTS.md. This closes the duplication tracked in #20.
