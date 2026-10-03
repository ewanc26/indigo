# Changelog

All notable changes to Indigo are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). `scripts/release.sh`
publishes the section matching the version you pass it.

## [Unreleased]

### Added
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
