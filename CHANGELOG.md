# Changelog

All notable changes to Indigo are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Every pull request
adds its entry under `## [Unreleased]`, linking itself. A release PR renames
that section to the version, and the Release workflow publishes that section
as the release notes.

## [Unreleased]

### Fixed

- The timeline header's button hints are drawn smaller, so "START Exit" no longer runs into the post counter on the top screen (seen in Azahar). The layout's character-width estimate is too narrow for the system font; this is the minimum change, not a font change. ([#71](https://github.com/ewanc26/indigo/pull/71))

## [0.12.0] - 2026-10-07

What has and has not been run, plainly: the host tests pass (4035 checks), the warnings sweep is clean, and the 3DS build links against Wolfram v0.37.0. The video poster has not been seen on a 3DS or in Azahar; the snapshot harness renders it with a stand-in font, so the widths there are approximate.

### Added

- A video in a post shows its poster frame, decoded at the size it is drawn (400 px at most), with a line saying it cannot play on the 3DS. The poster and its shape come from Wolfram v0.37.0's `wf_post_embed`. ([#69](https://github.com/ewanc26/indigo/pull/69))

## [0.11.0] - 2026-10-07

What has and has not been run, plainly: the host tests pass (4029 checks) and the 3DS build links against Wolfram v0.36.2. The new tabs have not been run on a 3DS or in Azahar.

### Added

- A person's posts screen has replies, media and (on your own account) likes tabs: tap the header box to move to the next one. The tabs, their filters and the fetch are Wolfram v0.36's `wolfram/profile_tab.h`. ([#67](https://github.com/ewanc26/indigo/pull/67))

## [0.10.0] - 2026-10-07

What has and has not been run, plainly: the host tests pass (4019 checks under AddressSanitizer and UBSan), all 148 snapshots render, and the 3DS build links in CI against Wolfram v0.35.0. The new image attachment (picker, alt text, upload) has been run on the host only, not on an emulator or a real 3DS, and the upload has never reached a server from Indigo. The code moved into smaller files in this release; its layout renders byte-identically to 0.9.0 but the worker thread and touch handling were not re-run on a console.

### Fixed
- A release whose version number is too long to show is now refused as unreadable instead of being cut short in the update screen. ([#60](https://github.com/ewanc26/indigo/pull/60))

### Added
- I can attach one picture to a post, reply or quote: SELECT, or the "Add an image" button in compose, opens a list of the JPEG and PNG files in `sdmc:/3ds/indigo/images` (under 950 KB), then asks for alt text. Wolfram does the filtering and the upload. The upload has not been run on an emulator or a 3DS. ([#58](https://github.com/ewanc26/indigo/pull/58))

### Changed
- The feed picker reads the saved feeds through Wolfram v0.35.0's `wf_agent_get_saved_feeds`; my copy of the preferences walk is deleted. ([#65](https://github.com/ewanc26/indigo/pull/65))
- Reply gates are set with Wolfram v0.34.0's `wf_agent_set_reply_gate`; my copy of the threadgate rules is deleted. ([#59](https://github.com/ewanc26/indigo/pull/59))
- What kind of failure a call was (wrong credentials, no network, a slow service, a TLS problem, a rate limit, a server fault, a reply I could not read) is Wolfram's `wf_failure_classify` and `wf_failure_tag` now, instead of a copy of them in `session.c` and `errors.c`; only the wording stays here. Nothing you would see changes ([#56](https://github.com/ewanc26/indigo/pull/56), [#20](https://github.com/ewanc26/indigo/issues/20)).
- Muted-word matching and the RFC 3339 timestamp code are Wolfram's now, not copies of Cobalt's: the matcher is `wf_muted_words_match` and the clock-free parsing and formatting are `wolfram/time.h`. A mute's expiry is checked at match time with Wolfram's parser, which also reads numeric offsets (the old one refused them). My copies, about 250 lines, are deleted. The host build now needs a Wolfram checkout and cJSON's headers ([#54](https://github.com/ewanc26/indigo/pull/54), [#20](https://github.com/ewanc26/indigo/issues/20)).

## [0.9.0] - 2026-10-07

What has and has not been run, plainly: the host tests pass (4000 checks under AddressSanitizer and UBSan) and the 3DS build links in CI against Wolfram v0.30.0. The new scrolling (touch drag, held D-pad and Circle Pad, C-Stick paging) has not been run on an emulator or a real 3DS: I cannot send touch or stick input to the emulator from here.

### Changed
- Indigo now builds against Wolfram v0.30.0 (was v0.29.0). ([#50](https://github.com/ewanc26/indigo/pull/50))

### Added
- Feeds scroll by dragging a finger up or down the list on the touch screen, a row for every row's height dragged, and the D-pad and Circle Pad repeat while held, so running down a feed no longer takes a press per post. The C-Stick pages on a New 3DS. A drag that begins on a button or the header does not scroll. Wolfram's `wf_drag` does the tap-versus-drag work. Checked on the host; not on an emulator or a console, where I cannot send it a touch ([#50](https://github.com/ewanc26/indigo/pull/50)).

## [0.8.1] - 2026-10-07

What has and has not been run, plainly: the host tests pass (3993 checks under AddressSanitizer and UBSan), the 3DS build links in CI against Wolfram v0.29.0, and I ran this build in Azahar on a signed-in session, where the timeline's avatars load and draw square and upright. It has not been run on a real 3DS.

### Changed
- Indigo now builds against Wolfram v0.29.0 (was v0.28.0). ([#48](https://github.com/ewanc26/indigo/pull/48))

### Fixed
- Avatars load. Four things stopped them, none visible on the host: the AppView's avatar URLs are about 130 bytes and Indigo cut them to 128, so the CDN answered 400; a URL with no format comes back as WebP, which the decoder cannot read; the first JPEG decode crashed the loader thread (stb_image's thread-local state, fixed in Wolfram); and the decoded image was copied into the GPU texture without being tiled. Indigo now asks the CDN for the 128 px avatar thumbnail or the feed thumbnail as a JPEG, URL buffers are 160 bytes, and textures are tiled. The profile screen also used the avatar's blob key instead of its URL. Checked in Azahar on a signed-in session; not on a console ([#48](https://github.com/ewanc26/indigo/pull/48)).

## [0.8.0] - 2026-10-06

What has and has not been run, plainly: everything here passes the host tests
(3983 checks under AddressSanitizer and UBSan) and builds and links for the 3DS
in CI against Wolfram v0.28.0. The signature check has not been run on an
emulator or a real 3DS. This is the first release whose `update.json` is signed,
so an Indigo that predates the check still takes releases on the SHA-256 alone.

### Changed
- Indigo now builds against Wolfram v0.28.0 (was v0.27.0). ([#46](https://github.com/ewanc26/indigo/pull/46))

### Added
- Updates are signed. Each release's `update.json` gets a detached Ed25519 signature, `update.json.sig`, made by the Release workflow with a key that exists only as a repository secret. Indigo checks it against the public key built in before it reads the manifest, and refuses a release that has no signature or the wrong one, so a replaced release no longer passes on its SHA-256 alone. Wolfram's `wf_update_verify_signature` does the check. Releases up to 0.7.0 are unsigned. None of it has run on an emulator or a console ([#24](https://github.com/ewanc26/indigo/issues/24)).

## [0.7.0] - 2026-10-06

What has and has not been run, plainly: everything here passes the host tests
(3941 checks under AddressSanitizer and UBSan) and builds and links for the 3DS
in CI against Wolfram v0.27.0. None of it has been run on an emulator or a
real 3DS: not the update screen, not browser sign-in through the new pairing
client, and not the icon.

### Added
- Indigo can update itself. More, then "Check for updates", asks GitHub for the latest release and shows its version, and nothing is downloaded until I press Install. The download is checked against the size and SHA-256 the release states, written to the card, read back and checked again, and only then swapped in, keeping the old build until the new one has started. A build that is not a plain release, or was not started from a `.3dsx` on the SD card, says it cannot update and why. It is not signed, so it does not prove a release is mine, and none of it has run on an emulator or a console. ([#43](https://github.com/ewanc26/indigo/pull/43))

### Changed
- Browser sign-in now uses Wolfram's pairing client rather than Indigo's own copy, so if the sign-in node has forgotten the pairing (it restarted), Indigo says so at once instead of polling for nine minutes. Indigo now builds against Wolfram v0.27.0. ([#42](https://github.com/ewanc26/indigo/pull/42))
- A release can be started without pushing a tag: `scripts/release.sh --dispatch` runs the Release workflow, and GitHub creates the tag itself, which I needed because tag pushes from the agents' sandbox are refused. ([#41](https://github.com/ewanc26/indigo/pull/41))

## [0.6.0] - 2026-10-06

What has and has not been run, plainly: everything here passes the host tests
(3957 checks under AddressSanitizer and UBSan) and builds for the 3DS in CI.
None of it has been run on an emulator or a real 3DS, including the new icon,
who liked or reposted a post, and the start-up update recovery.

### Added
- Who liked a post and who reposted it, from the post menu, as paged lists of
  people with avatars, the same as followers. This was the one thing on
  Cobalt's list that Indigo lacked.
  ([#12](https://github.com/ewanc26/indigo/pull/12))
- The draft of an unsent post survives closing Indigo, or the battery running
  out. ([#12](https://github.com/ewanc26/indigo/pull/12))
- A logo and a Homebrew Menu icon: an indigo bunting on a twig, in pixel art.
  `tools/gen_logo.py` draws them all from one set of shapes, and draws the
  24x24 small icon at its own size rather than letting it be averaged down from
  the 48x48 one. ([#28](https://github.com/ewanc26/indigo/pull/28),
  [#29](https://github.com/ewanc26/indigo/pull/29))
- The groundwork for updating from inside Indigo (docs/UPDATE.md). Each release
  now carries `update.json`, a versioned `indigo-<version>.3dsx` and its
  `.sha256`. At start-up Indigo finishes or undoes an update that was
  interrupted, and keeps the old build until the new one has started. The
  screen that offers an update is not in yet; it waits on Wolfram's shared
  update module. ([#27](https://github.com/ewanc26/indigo/pull/27))
- docs/PARITY.md, saying what Indigo does next to Cobalt and where each thing
  has been checked, with app-password and browser sign-in as separate rows.
  ([#21](https://github.com/ewanc26/indigo/pull/21))
- Everything a Universal-DB listing needs, generated: the entry, a 256x128
  banner, and screenshots rendered from the layout code, plus
  `scripts/udb-submit.sh` to open the pull request in my name.
  (docs/UNIVERSAL-DB.md, [#36](https://github.com/ewanc26/indigo/pull/36))

### Changed
- The text-size setting now applies to post bodies.
  ([#12](https://github.com/ewanc26/indigo/pull/12))
- The settings screen says reduce motion is reserved. Indigo draws no
  animation yet, so the toggle changes nothing, and the screen no longer
  implies otherwise. ([#16](https://github.com/ewanc26/indigo/pull/16))
- Releases are built and published by CI from the tag, against a pinned
  Wolfram release (`wolfram.ref`, currently v0.26.0). `scripts/release.sh` only
  checks and tags, so nothing is uploaded from my machine any more.
  ([#32](https://github.com/ewanc26/indigo/pull/32))
- Changes go through pull requests checked by the flow shared with the other
  four projects, and are merged by rebase.
  ([#17](https://github.com/ewanc26/indigo/pull/17),
  [#27](https://github.com/ewanc26/indigo/pull/27),
  [#29](https://github.com/ewanc26/indigo/pull/29))

## [0.5.0] - 2026-10-04

### Added
- Search results page in. Actor search, post search, a profile's posts,
  followers and following, lists, list members, mutes and blocks all grow
  more results as the cursor nears the end of what is held -- the same
  prefetch rule the timeline uses, so scrolling does not pause at a page
  boundary. Pages append rather than replace, a page that brings back
  nothing ends the paging, and the list stops asking at sixty entries.
  Saved feeds are the one exception: they come from the account's
  preferences in a single fetch, so there is no second page to ask for.
- Muted words and "hide reposts" are honoured in the home timeline and custom
  feeds. The account's saved preferences are fetched once per sign-in, and a
  post whose text or tags carry a muted word is dropped from the page before
  the app ever sees it. An expiry in the past is skipped rather than honoured,
  a single alphanumeric word matches whole words only (muting "cat" does not
  hide "category"), and a phrase matches as a substring. Hide-reposts applies
  to the home timeline only, which is what the preference means. The rules are
  the same ones Cobalt settled, so the two clients do not diverge on what a
  person asked to stop seeing. A failed preferences fetch is not fatal: the
  feed is shown unfiltered and the next page tries again.
- A full-size image viewer. With a post that has a picture selected, the
  timeline, the thread and a post in the search results grow an `Image` button
  in the header bar, and ZR does the same on a New 3DS. The picture is drawn on
  the top screen fitted to the whole 400x240 and centred, from the aspect ratio
  the server declared, so a portrait uses the screen's height and a panorama
  its width. The description of the picture is on the bottom screen -- the one
  with room for a sentence -- and follows the alt-text setting, and a post of
  several photographs says `1 of 4 images.` rather than implying it has one.
  B, ZR or `Close` goes back to the screen it was opened from.

### Changed
- The image cache will decode a post's picture up to 400px, the top screen's
  width, instead of 256px. The detail band still asks only for the size it
  draws, so nothing else changes; the viewer is the screen that draws one
  across the full width, and a 256px copy of that is an enlargement.
- The GPU texture pool is bounded by bytes as well as by count, and holds
  twelve textures rather than eight. A texture is power-of-two padded, so an
  avatar costs 128x128 and a full-size photograph 512x512: eight of the latter
  would be 32MB of texture memory, which the console does not have, while eight
  avatars is half a megabyte.

### Internal
- Opening the viewer drops the picture's cache slot so the next request decodes
  it at the size the viewer draws. First-request-wins cuts both ways: a portrait
  photograph the detail band drew 60px tall would otherwise still be 60px on a
  240px screen. The screen that wanted the smaller copy is not on screen while
  the viewer is, and the button it was drawn for is not either.
- The header bar's status text moves to the top screen's title bar while the
  `Image` button is up. A status is only ever a transient "Loading...", a
  "Nothing here yet" for a list with no posts, or a fetch error, and the first
  two cannot happen while a post is selected -- so the button only ever
  displaces an error, which is worth more of the top bar's room than the
  reminder of which key reloads the list.

## [0.4.0] - 2026-10-04

### Added
- Browser-based AT Protocol OAuth sign-in through the hosted Wolfram OAuth node, with a short-lived pairing link that can be opened on another device. The PDS handles the account password and MFA; Indigo never sees them.
- Post images. A post with an `images` embed draws its first picture in a box
  of the shape the server declared, and the post's text gives up three of its
  five lines to make room for it. One image of up to four is drawn and the count
  is stated on screen, because a post of four photographs is a different thing
  from a post of one. The box is fitted on both axes, so a 1:3 panorama and a
  portrait both stay inside the band.
- Link cards. A post with an `external` embed draws a surface with the link's
  title, its URI and the site's own picture when it has one. Indigo cannot open
  a link yet, so the card is something to read rather than something to press.
- An `Image alt text` setting, off by default, that draws a post image's alt
  text under the picture. Alt text is written for a screen reader, so this is a
  choice rather than a caption under every photograph. The description takes the
  lines it needs out of the bottom of the band and the picture is fitted into
  what is left.

### Changed
- OAuth-node sessions are persisted separately from ordinary PDS sessions and resumed through Wolfram's hosted authentication path.
- The image cache's decode cap is now per request rather than one 96px cap for
  everything, and its byte budget is 1.5MB so that a handful of full-size
  thumbnails fit. An avatar still decodes at 96px; a thumbnail asks for what it
  is drawn at.

### Notes
- A post's image and link card are drawn on the detail surfaces -- the home and
  thread post, and a post in the search results. List rows are unchanged: a row
  is two lines of text and a face, and there is no honest way to fit a
  thumbnail into one.

### Internal
- Added the unified hosted OAuth node to Wolfram and wired Indigo to it as a thin 3DS client.
- The settings screen has eight rows and fits them by 2px of row height and a
  3px upward nudge of the first row; the gap between rows is unchanged, because
  the gap is what decides whether a thumb lands on the wrong row.
- `indigo_layout_hit()` no longer builds a 370KB `indigo_app` on the stack to
  read one enum out of it.
- The release is tagged before it is built, so the `.3dsx` names its own release
  in `INDIGO_BUILD_COMMIT` rather than the previous tag plus a commit count.
  Releases before this one shipped an artifact that reported the tag they were
  about to become's predecessor.
- The settings screen prints the build identity -- tag, build number and date --
  at the foot of its top screen. The Makefile has stamped those three values
  since 0.1.0 and nothing read them, so the linker dropped all three and no
  build could name itself.

## [0.3.0] - 2026-10-04

### Added
- An image pipeline (`src/media`): a fixed-size, byte-budgeted decoded-image
  cache and a fetch/decode worker thread. Images are fetched with Wolfram's
  token-less public GET on a client of its own, decoded off the frame loop, and
  uploaded to the GPU on the main thread; nothing decodes or touches a texture
  where a frame is drawn.
- Avatars everywhere a row has a person in it: timeline and thread rows, the
  selected post's header, the profile header, the notification list and its
  detail, and every people row — search, list members, followers, following,
  mutes and blocks.
- A per-URL tinted placeholder for an image that has not arrived yet, so a
  timeline reads as a set of accounts rather than a column of grey squares.

### Fixed
- Wolfram: `wf_agent_profile` now carries the avatar URL that getProfile
  actually sends. Only the bare-string form was read, so against a real server
  the field stayed empty and a client had nothing to fetch for a profile's
  avatar.

### Notes
- Post images, link cards, alt text and a full-size image viewer are not in
  this release; the pipeline they need is.
- The 3DS build grows by about 112KB of code, which is the image decoder.

## [0.2.0] - 2026-10-04

### Added
- Interactive settings screen (`INDIGO_SCREEN_SETTINGS`) accessible from the
  More menu, providing toggling of theme, text scale, reduce motion, high
  contrast, large touch targets, diagnostics logging, and default startup feed.
- Accessibility settings wiring: theme palettes (Light, Dark, High Contrast)
  and touch target expansion (4px extra touch hit area) when large touch
  targets are enabled.
- Browsable mute and block lists ("Muted accounts" and "Blocked accounts" in the
  More menu).
- Reply gate selection on post compose ("Replies: Anyone / Followed / Mentioned /
  None" toggle).
- The account's saved custom feeds, as "Feeds" in the More menu, and one
  feed's posts on the home screen from SEL on a feed row. The picker reuses the
  search screen and the list rows -- a feed is a name with a URI to open, which
  is what a curated list is -- and the feed itself reuses the timeline, since
  getFeed returns the same feedViewPost items getTimeline does. Only the
  requests are new: getPreferences for the saved URIs (V2 first, the V1 list as
  the older fallback) and getFeedGenerators to name them. The preferences JSON
  is read rather than the typed parse, because one preference type the parser
  rejects must not take the whole picker down with it -- Cobalt hit exactly
  that on a real account. A feed view's B returns to the picker and puts the
  Following timeline back, matching Cobalt's timeline BACK; the More menu is
  then two presses away rather than one, and the bottom-right button says
  "Feeds" and does the same thing B does. The title bar carries the feed's
  name, so a name longer than the bar is cut with an ellipsis instead of
  running into the hint; the bounds test now measures four name lengths. Bounded
  to 20; the saved-feeds list is not paged, matching actor and post search.
- Curated lists, as "Lists" in the More menu, and one list's members from SEL
  on a list row. They reuse the search screen rather than adding two more:
  the lists are a third row type with their own array, and the members are
  people, so they reuse the actor rows the followers and following lists use.
  Back from members restores the lists by hand, because history holds screens
  rather than search kinds and this is the one place the search screen stacks
  on itself with a different kind -- the members are actors in the same union
  the lists live in, so without the restore Back would return to a corrupted
  screen. Bounded to 20; the getLists and getList cursors are dropped, the
  same reasoning as actor and post search.
- A pinned post, from a Pinned button on the profile. It costs no request:
  getProfile already returned pinnedPost.uri and Indigo was discarding it, and
  opening a thread needs nothing more than the URI. The button is drawn
  enabled only when the account actually has one.
- A person's own posts, from a Posts button on their profile. It reuses the
  post list that post search fills, the way the followers and following lists
  reuse the actor list: same rows, same navigation, same thread-opening on
  SEL. Only the request differs. The header names them, since there is nothing
  to type into an author's posts. Reposts carry their "Reposted by" line
  because the timeline's conversion is reused. Bounded to 20; a profile's posts
  are not paged, matching actor and post search.
- Post search, as "Find posts" beside "Find people". It shares the search
  screen rather than adding one: same rows, same navigation, same header box.
  The results are posts rather than people, so they share storage with the
  actor list through a union — an indigo_post is roughly twenty times an
  indigo_actor, and the 3DS should not pay for both. Rows lead with the author
  because a row of text is not identifiable by its text, and SEL opens the
  thread rather than a profile. Bounded to 20 like actor search; the cursor is
  dropped rather than kept for a page that will never be fetched.
- A person's followers and following, from the two count buttons on their
  profile. They reuse the search screen rather than adding two near-identical
  ones: same rows, same navigation, same result type, so only the request
  differs. The header names the subject instead of offering a query box,
  because there is nothing there to type.
- Mute, unmute, block and unblock from a person's profile. Mute is X and
  block is R on the profile screen, with both also on the touch buttons as a
  pair under Follow; the buttons name the action they perform rather than the
  state they are in, so the label never changes under the reader. Cobalt has
  had both, plus browsable mute and block lists, which this does not.
- Notifications are marked as seen after a successful fetch, so the unread
  badge does not linger on other clients. Indigo never pages notifications,
  so there is no backward-paging case to exclude the way Cobalt has to.
  `util/timefmt` formats the `seenAt` timestamp, and a clock that reports
  non-positive time skips the call rather than sending a 1970 date.
- Follow and unfollow from a person's profile: the bottom screen gets a
  full-width button and Y does the same thing, since the profile had been
  showing "Following" as read-only state with no way to change it. The state
  flips on the press rather than waiting for the server, and is put back if
  the write fails. Cobalt has had this since 0.x; this closes the gap.
- Wolfram's `getProfile` `did` and viewer-follow record URI are now kept. The
  did is what `follow` is addressed to, and unfollow deletes by URI with no
  handle to resolve it from, so neither can be recovered from the fields the
  profile already stored. The blocking URI and mute flag came in the same
  response and are what the mute and block actions above use.
- Find people (actor search, phase 5): a "Find people" entry in the More menu
  opens a search screen whose query box sits in the bottom-screen header, so
  the result rows keep the standard list geometry. Accepting the keyboard runs
  the search in the same step rather than making the person confirm again on a
  list screen. Results come from Wolfram's `wf_agent_search_actors_typed`, are
  bounded to 20 with no paging (a 3DS list that cannot show page two is not a
  list worth paging), and SELECT or the Profile pill opens the selected
  person's profile. The query and results survive leaving and reopening the
  screen; a changed query drops stale results so no one can open a profile the
  new query never matched. "Searched and found nobody" is a distinct state
  from "not searched yet". Four snapshot scenarios cover idle, results, no
  match and failure.

### Fixed
- The profile screen's status line was drawn at y=244 on a 240-tall bottom
  screen, so every follow, mute and block error message was invisible. The
  button rows are now spaced so the status line has room, and the layout
  spacing test asserts that room exists rather than only that each button is
  on screen — which is what let this through.
- A source file edited in the same second as the object it should replace was
  never recompiled, because `make` compares modification times at one-second
  granularity (the filesystem keeps nanoseconds; the comparison discards
  them). It surfaced as undefined references to functions that were defined
  and compiled in the same file, which reads like a missing definition rather
  than a stale object. Diagnose it by comparing the `.o` and `.c` timestamps;
  `make clean` clears it.
- The 3DS build was broken against a sibling Wolfram checkout on the
  `local/indigo-build` branch, which still carried the pre-merge
  `post_display.h`. PR #89 renamed that header's facet struct
  `wf_post_facet` -> `wf_display_facet` because `post_view_typed.h` declares a
  different facet of the old name, so the 3DS cross-build failed on a type
  Indigo had already been written against. `make test`, `make warnings` and
  `make snapshots` all still passed, since the adapter sits behind `__3DS__` —
  so facet targets have never actually been built for the console. Merging
  `main` into Wolfram's `local/indigo-build` resolves it; no Indigo change.

## [0.1.0] - 2026-10-03

### Added
- Threads, profiles, notifications, compose and the More menu (M4): the top
  screen shows the thread with the focused post marked, a profile, or the
  selected notification; the bottom screen is the corresponding touch list.
  Compose publishes a post, a reply or a quote, keeps the draft across cancel
  and failure, and refreshes the view it came from. The More menu is built
  from the post being read: its facet targets come first, labelled with the
  text the person sees, and a mention opens that person's profile; links and
  tags are shown because nothing here can open a browser. The menu scrolls
  when a post has more targets than fit and shows its position.
- Facet targets (mention did, link URI, tag) are now carried through from
  Wolfram's `wf_post_facet.target`; previously only the byte ranges were
  kept, so a facet could be coloured but not acted on.
- Timeline (M3): top screen shows the selected post (name, handle, wrapped
  text with link/mention/tag colouring, embed note, counts); bottom screen
  is a touch list with Like, Repost and Reload. D-pad/L/R move, Y likes,
  X reposts, SELECT reloads. Posts are fetched in pages of 15 on the worker
  thread, prefetched near the end, and capped at 60 in memory. Post text,
  facets, embeds and "reposted by" come from Wolfram's `post_display` API.
- Sign-in (M2): service/handle/app-password form on the touch screen, a
  worker thread for login so the UI never blocks, a curated CA bundle in
  romfs (`tools/make_cabundle.py`), a versioned session file with atomic
  save and corrupt-file quarantine, auto-resume on launch, sign-out, clear
  error messages, and a log file on the SD card.
- `DEV_AUTOFILL=1` emulator-only build option.
- Display-list canvas (`src/gfx`) and a pure layout module, so screens are
  laid out without any platform API and replayed by a backend.
- Host unit tests (`make test`, ASan + UBSan, warnings as errors), a
  warnings-as-errors sweep (`make warnings`) and a host snapshot renderer
  (`make snapshots`) that writes PNGs of both screens.
- `make run-emu EMU=...` to launch the `.3dsx` in Azahar, Citra or Lime3DS,
  and `make wolfram-3ds` to build the sibling Wolfram checkout for 3DS.
- Touch-activated bottom-screen buttons with a shared hit-test.
- CI for host checks and the devkitARM build, and `scripts/release.sh`.

### Fixed
- The original Makefile lacked the 3DS link rules and could not produce a `.3dsx`.
- `make` never rebuilt the `.3dsx` after a source change: the outer target had no
  dependency edges, so it reported "Nothing to be done" over a stale executable.
  The recursive make now runs on every build.
- `make run-emu` launched Azahar's binary directly, which silently ignores a
  `.3dsx` on its command line and sits on its HOME menu; macOS app bundles are
  now opened through LaunchServices.
- The host snapshot renderer treated its "no posts" scenario as a populated one,
  so the More menu appeared with facet targets even when nothing was selected.
- Requires the Wolfram fix aligning its `soc_ctx` buffer to a page; without it
  Azahar aborts with a kernel assertion when `wf_platform_init` runs.

### Changed
- `make build-3ds` builds the `.3dsx` in the devkitPro container that CI uses,
  because the 3DS portlibs (curl, mbedtls, zlib) ship in that image and not in a
  plain host devkitPro. `make` is still the host build.
- The README documents the one font caveat that matters for screenshots: Azahar
  has no 3DS system shared font and substitutes its own, so text metrics in an
  emulator capture are not the console's.
- Button hints are stated exactly once per screen: the bottom-screen pills name
  the list actions and the top title bar only what they cannot. No screen hints
  the same control on both displays any more, and the home title bar fits its
  width again.
- The 3DS renderer now replays display lists instead of drawing directly, and
  clears the text buffer once per screen (it was cleared per string).
- The Wolfram adapter initialises Wolfram's 3DS platform layer and proves the
  link with a pure library call.

[Unreleased]: https://github.com/ewanc26/indigo/compare/v0.4.0...HEAD
[0.4.0]: https://github.com/ewanc26/indigo/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/ewanc26/indigo/compare/v0.2.0...v0.3.0
