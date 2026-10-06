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
| OAuth through a hosted node | implemented in `do_oauth()` (`src/atproto/session.c`): a handle with an empty password calls `uk.ewancroft.oauth.begin`, shows a pairing URL and code, polls `uk.ewancroft.oauth.poll`, keeps a node bearer token | **not verified anywhere**: no host test (the host build has no JSON parser and the flow is network code), no emulator run, no hardware. The only record is the PR that added it | a Wolfram OAuth node (its `oauth-node.md` page) whose URL goes in the service field. bsky.social is not a node, so the default service URL cannot do this |

OAuth here is not the AT Protocol browser flow running on the console. The node holds the OAuth session and DPoP key; the console holds a bearer token for the node. A node outage ends the session, and the account's PDS never sees Indigo directly.

The pairing contract (field names, terminal and transient failures, test vectors, no tokens in logs) is filed for Wolfram as ewanc26/wolfram#101, so the console clients stop carrying their own copies. Until that lands, `scripts/check-flow.sh protocol` keeps the two pairing method names as the only raw protocol strings in `src/`.

Known weakness: `do_oauth()` treats every failed poll as transient and keeps polling for up to nine minutes, including a 404 for an unknown pairing code (a restarted node forgets pairings). That belongs in the shared driver, not in a local patch.

## Features

| Feature | Cobalt | Indigo | Verification and notes |
|---|---|---|---|
| Diagnostics | yes | implemented: a diagnostics setting gates `sdmc:/3ds/indigo/indigo.log`; the settings screen names the build | host; log never holds tokens |
| Persistent session, sign-out | encrypted | implemented, plaintext on the SD card by decision (AGENTS.md section 12) | emulator: save, resume, sign-out |
| Home timeline, paging, reposts, threads | yes | implemented | emulator against a live account |
| Compose, reply, quote | yes | implemented | emulator: modes 0, 1 and 2 published |
| Reply gates | yes | implemented (`src/app/social.c`: everyone, following and mentioned, nobody) | host only |
| Like and repost, with undo | yes | implemented | emulator, counts moved and were undone |
| Notifications, mark as seen | yes | implemented | emulator fetch; seen marking host only |
| Profiles, follow, unfollow | yes | implemented | host |
| Followers, following | yes | implemented, paged | host |
| Profile tabs | yes | partial: a person's posts only (`INDIGO_SEARCH_AUTHOR`) | host; no replies, media or likes tab |
| Pinned posts | yes | implemented | host |
| Avatars | yes | implemented | host; the texture upload has not been seen on a screen |
| Images on posts, replies and quotes, with alt text | yes | implemented for viewing, behind an alt-text setting | host only |
| Full-size image viewer | yes | implemented | host only |
| Who liked or reposted a post | yes | implemented (`INDIGO_SEARCH_LIKED_BY`, `INDIGO_SEARCH_REPOSTED_BY`; post menu) | host and 3DS build in CI; #15 closed as already done |
| Link-card previews | yes | implemented | host only |
| Actor search, post search | yes | implemented, paged | host |
| Custom feeds | yes | implemented: the account's saved feeds, one feed on Home | host |
| Lists and members | read-only | implemented, read-only | host |
| Mute and block, with lists | yes | implemented | host |
| Muted words, hide reposts | yes | implemented, same rules | host |
| Attach an image when composing | yes | issue: #19 | Wolfram already has `wf_agent_upload_blob_ex` |
| On-card cache of timeline and profile data | not listed | issue: #13 | not started |
| Settings screen | n/a | implemented: theme, text scale, alt text, diagnostics, startup feed; reduce motion and high contrast are stored and reserved | host |
| Video, GIFs and animated media | not planned | not planned: Wolfram's decoder (`wf_image_decode_rgba`) is for still images, and nothing in the 1.5MB image budget accounts for frames | no decoder evaluation has been done, so this is a decision and not a hardware limit |
| Auto-update | in progress, to the same wolfram#106 contract | partial: release assets (`update.json`, checksum) and start-up recovery of an interrupted swap are in; the confirm-and-download screen waits on wolfram#106 | host only; see docs/UPDATE.md. Not run on an emulator or hardware |
| Push notifications | not planned | not planned: no push service a homebrew application can register with; notifications are fetched when the screen is opened | |

## Duplication with Cobalt

Tracked in #20. Short version: muted-word matching, relative time and the OAuth pairing client exist in both repositories as copies and belong in Wolfram.
