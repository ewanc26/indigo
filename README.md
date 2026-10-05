# Indigo

A native Bluesky client for the Nintendo 3DS.

**Version 0.5.0**

Indigo exists because apparently making a Wii U post to Bluesky was not enough.

After forcing a PowerPC tri-core console to participate in the modern social
web with [Cobalt](https://github.com/ewanc26/cobalt), I decided that the next
reasonable target was a Nintendo 3DS: another machine with absolutely no
business being a Bluesky client.

The goal is the same kind of hardware-first absurdity, but the implementation
is deliberately different. Indigo is **not a port** of Cobalt. I am designing
it around the 3DS's own hardware, input model and two-screen layout.

The project uses [Wolfram](https://github.com/ewanc26/wolfram), my C AT Protocol SDK, for protocol functionality. Indigo is scoped to the Bluesky app, not general AT Protocol use; accounts on custom PDSes are supported.

## Status

**0.5.0** ([tag](https://github.com/ewanc26/indigo/releases/tag/v0.5.0)) adds a full-size image viewer, muted words, and paged search. An `Image` button appears in the header bar of the timeline, the thread and a post in the search results whenever the selected post has a picture on it, and ZR does the same on a New 3DS. The picture fills the top screen at the shape the server declared, its description is on the bottom screen where there is room for a sentence, and `B`, `ZR` or `Close` goes back to the screen it was opened from. The account's saved muted words and hide-reposts are honoured in the home timeline and custom feeds, fetched once per sign-in, with the same matching rules Cobalt uses. Search results now page in -- actor search, post search, a profile's posts, followers and following, lists, list members, mutes and blocks all grow more results as you scroll, up to sixty entries.

On top of **0.4.0**'s browser-based OAuth sign-in through a hosted pairing node -- the PDS handles the password and MFA, and Indigo never sees either -- post images, link cards, an alt-text setting, timelines with paging, threads, profiles, notifications, compose with replies and quotes, likes and reposts, actor and post search, curated lists, saved custom feeds, and mute and block.

Each release is a tag on this repository with the `.3dsx` attached. The changelog is in [CHANGELOG.md](CHANGELOG.md), the design decisions and their reasons in [AGENTS.md](AGENTS.md), and open work in the [issue tracker](https://github.com/ewanc26/indigo/issues).

| Check | State |
|---|---|
| Host unit tests, warnings-as-errors sweep, snapshot renderer | verified on the host: 2949 checks, 0 failures, with AddressSanitizer and UBSan, and a warnings-as-errors sweep over every host-portable source. `make test` raises its own stack limit, because the suite peaks well past the default shell's 8MB |
| TLS to the real service with the bundled CA roots | emulator-verified in Azahar: handshake to `https://bsky.social` succeeds and the server's rejection of a bogus login shows as "Wrong handle or app password." ([screenshot](docs/screenshots/m2-azahar-signin-error.png)) |
| Sign-in to a custom PDS (eurosky.social), session save, resume on restart, sign-out | emulator-verified in Azahar: signed in, session saved, relaunch resumed it without credentials, Sign out deleted the session file and returned to the sign-in screen ([signed out](docs/screenshots/m3-emulator-signed-out.png)) |
| On-screen keyboard (swkbd) entry | **not verified** in the emulator (credentials were supplied by the dev autofill file) |
| Timeline model, wrapping, link spans, selection, paging triggers, like/repost state | verified on the host; both screens rendered by the host snapshot renderer ([top and bottom](docs/screenshots/m3-snapshot-timeline.png), [scrolled](docs/screenshots/m3-snapshot-timeline-scrolled.png)), which are **not emulator output** |
| M3 build boots in Azahar | emulator-verified: the sign-in screen still renders ([screenshot](docs/screenshots/m3-azahar-boot.png)) |
| Timeline fetch, paging, reload, like, unlike, repost, unrepost against the real service | emulator-verified in Azahar on the live account: first page, a second page (26 posts, [screenshot](docs/screenshots/m3-emulator-paging.png)), reload, and like/unlike and repost/unrepost with the counts moving by one each way ([liked](docs/screenshots/m3-emulator-liked.png)); every like and repost was undone afterwards |
| Thread fetch, profile fetch, notifications fetch, compose of a post, a reply and a quote | emulator-verified in Azahar against the live account: `indigo.log` on the virtual SD records `thread: 2 posts`, `notifications: 30` and `published (mode 0)`, `(mode 1)` and `(mode 2)` |
| More menu built from the post being read, with its facet targets, scrolling, and choosing a mention to open that profile | verified on the host (including label text, payload, scrolling bounds, out-of-range facet ranges and the touch/action mapping); both screens rendered by the host snapshot renderer ([facet targets](docs/screenshots/m4-snapshot-menu-facets.png), [scrolled](docs/screenshots/m4-snapshot-menu-facets-scrolled.png)), which are **not emulator output**. The menu itself was opened in the emulator and both screens rendered ([more menu](docs/screenshots/m4-azahar-more-menu.png)), though the capture cannot be read back here to confirm which post's targets it listed |
| M4 build boots in Azahar and renders both screens | emulator-verified: `make run-emu` builds, launches, resumes the saved session and fetches a page; the top screen shows the selected post and the bottom screen the touch list ([both screens](docs/screenshots/m4-azahar-home.png)) |
| Every control is hinted exactly once, and no text, rect or image runs off a screen | verified on the host: a test walks both display lists for every screen and fails if a button glyph is hinted on both displays, or if any text, rect or image exceeds its canvas |
| Avatars and the image pipeline behind them | verified on the host (2659 checks): the decoded-image cache's claim/publish/fail/evict rules, its byte budget, the per-slot generation that drops a result whose slot was reused, its refusal to evict anything still in flight, and that a row with no avatar emits no image command at all. Avatars are drawn on every row that has a person in it. The decode path itself — Wolfram's `wf_image_decode_rgba` — is verified by Wolfram's own suite, and both backends draw the same per-URL tinted placeholder for an image that has not arrived yet. **not emulator-verified**: no Azahar here, so the fetched-and-uploaded texture has not been seen on a screen |
| Post images and link cards | verified on the host: a post's image is drawn in a box of the shape the server declared — six ratios including portrait and panorama, fitted inside the band on both axes, which is what a `1:3` panorama and a square both need — and the post's text gives up three of its five lines to make room. A link card is a surface with the title, the URI and the site's own picture; the title and URI are kept inside the card, and the picture takes its width from the title's. An embed that cannot be drawn falls back to the one-line note, and an embed with no URL draws nothing rather than an empty box. **not emulator-verified**: no Azahar here |
| Alt text | carried on every image post (`indigo_post.embed_alt`) and drawn when the `Image alt text` setting is on, which is off by default: alt text is written for a screen reader, and a reader looking at a photograph does not also want its description. It takes as many lines as it needs out of the bottom of the band -- one sentence costs one line -- and the picture is fitted into what is left, down to a floor of 30px, so turning it on never turns the picture into a line. Persisted in the settings file, version unchanged: a file without the key takes the default |
| The full-size image viewer | verified on the host: the viewer opens only from a post that has a picture -- a link card, a quote, a post with no embed and an image post with no URL all refuse -- and from the timeline, the thread and a post result, in each case opening the *selected* post and copying it rather than pointing at it, because the list it came from keeps paging. The picture is drawn fitted inside the top screen and centred, for a portrait, a landscape, a square and a declared nothing. The description follows the alt-text setting; a post of four photographs says `1 of 4 images.`; a picture with no description says so. Only `Close` is a control, so a thumb resting on the screen cannot close it. The button follows the selection, takes the status line's slot, and does not overlap the title or `Back`. **not emulator-verified**: no Azahar here, and the decode-at-the-viewer's-size path (`indigo_media_forget` then a fresh claim) has not been seen on hardware |
| Decoded image memory | bounded by construction: 24 slots, a 1.5MB total budget, a 96px avatar decode cap, a 400px thumbnail cap and a 256KB download cap, checked before anything is allocated. The budget holds about five full-size thumbnails; at the thumbnail cap all 24 slots would be ~6MB, so the budget is the real bound. The GPU side is bounded too: twelve textures and 2MB, because a texture is power-of-two padded and a full-size photograph is a 512x512 where an avatar is a 128x128. The 3DS build grows by 112KB of code, which is stb_image's decoder |
| Real hardware | never run |

Sign-in has two flows, and the second is not a replacement for the first. With an app password, it takes a service URL (default `https://bsky.social`), a handle and the app password, and follows the account's PDS; that is the one I have run on an emulator. With an empty password it starts browser sign-in through a hosted Wolfram OAuth node, and then the service URL has to be that node, because bsky.social does not serve the pairing calls. The OAuth flow has not been run on a host test, an emulator or hardware. The state of every feature against Cobalt is in [docs/PARITY.md](docs/PARITY.md). The session is saved to `sdmc:/3ds/indigo/session.dat` as plaintext: the SD card has no permissions, and an obfuscation key stored beside the file would be false comfort. Use an app password, never your main password. Logs go to `sdmc:/3ds/indigo/indigo.log` and never contain tokens or passwords.

Development and verification happen on the host and on an emulator; nothing here has been tested on real 3DS hardware. Sound, sleep, the HOME menu, the icon and the banner are untouched.

### Emulator sign-in autofill

`make DEV_AUTOFILL=1` builds a binary that reads `sdmc:/3ds/indigo/autofill.txt` (`service=`, `handle=`, `password=` lines) and submits it, so the form can be exercised without the keyboard. It is for the emulator only and is never part of a normal build. Delete the file afterwards.

## Requirements

Indigo targets real Nintendo 3DS hardware through the normal homebrew development stack:

- devkitPro/devkitARM
- libctru
- citro3d
- citro2d
- the required 3DS portlibs
- a sibling checkout of [Wolfram](https://github.com/ewanc26/wolfram) built for 3DS

The official devkitPro 3DS examples and package set are useful references for keeping the project aligned with current homebrew tooling.

## Building

Build Wolfram for 3DS first:

```sh
git clone https://github.com/ewanc26/wolfram ../wolfram

cd indigo
make wolfram-3ds   # runs Wolfram's own CMake toolchain file for 3DS
make
```

Indigo looks for Wolfram at `../wolfram/build-3ds` by default. Set `WOLFRAM_ROOT` and `WOLFRAM_BUILD` to override those paths.

### Build targets

| Command | Purpose |
|---|---|
| `make` | Build the 3DS application |
| `make wolfram-3ds` | Build the sibling Wolfram checkout for 3DS |
| `make run` | Print the SD-card installation path |
| `make run-emu` | Build, then launch the `.3dsx` in an emulator (`EMU=` selects it) |
| `tools/emu-shot.sh FILE.3dsx` | Screenshot the emulator window on macOS (see [Capturing the emulator window](#capturing-the-emulator-window)) |
| `make test` | Host unit tests (ASan + UBSan, warnings are errors); no devkitARM needed |
| `make warnings` | Warnings-as-errors sweep of every host-portable source |
| `make snapshots` | Render PNGs of both screens to `build-host/snapshots/` |
| `make clean` | Remove generated build output |

### When `make` misses a change

`make` compares modification times at one-second granularity. The filesystem
stores nanoseconds, but the comparison does not use them: a source file 1ms
newer than its object does not trigger a rebuild, while the same file 1.1s
newer does. So a source saved in the same second as the object that should
replace it is treated as up to date.

The symptom is a link error naming functions that are plainly defined and
compiled in the very file you just edited:

```
/opt/devkitpro/devkitARM/lib/gcc/arm-none-eabi/16.1.0/../../../../arm-none-eabi/bin/ld: main.o: in function `handle_events':
undefined reference to `indigo_app_set_query'
```

The object was never recompiled, so this is a stale object rather than a
missing definition. Confirm it before changing any code:

```sh
ls -la build/app.o src/app/app.c      # equal or object-newer means stale
make clean && make                    # or: touch the source, if the tree is otherwise clean
```

It is easy to hit after a build that failed partway through, or when a save
and a build land in the same second.

### Emulator

```sh
make run-emu                                  # Azahar at ~/Applications/Azahar.app
make run-emu EMU=/path/to/citra               # Citra or Lime3DS as fallbacks
```

Azahar keeps its virtual SD card at `~/Library/Application Support/Azahar/sdmc` on macOS (the `sdmc:` root; Indigo writes `3ds/indigo/indigo.log` and `session.dat` there). Its own log, including the font warning below, is at `~/Library/Application Support/Azahar/log/azahar_log.txt`.

Emulator results are emulator-verified only. TLS, certificates and DNS can behave differently from hardware, and emulator performance must never drive tuning. The 3D slider, sleep and the Home menu are left to the hardware pass.

One font caveat is worth knowing before trusting any screenshot: Azahar has no 3DS system shared font and logs `Shared Font file missing. Loading open source replacement from memory` on every launch. Indigo draws with the 3DS system font, so glyph coverage and advance widths in an emulator capture are Azahar's substitute, not the console's. Layout and behaviour can be checked in the emulator; text metrics cannot.

#### Capturing the emulator window

Azahar has no `--screenshot`, and `--dump-video` writes nothing on macOS, so a capture has to come from the window itself:

```sh
tools/emu-shot.sh indigo.3dsx                       # -> build-host/emu-shots/shot-<timestamp>.png
tools/emu-shot.sh --out /tmp/az.png --keep indigo.3dsx
```

Three things make this work, each of which is a failure mode worth knowing about:

- **Launch through LaunchServices.** `MacOS/azahar file.3dsx` ignores the argument, never opens a window and sits on its HOME menu. The script uses `open -a`, the same route as `make run-emu`.
- **Wait for the window, and expect ~30s.** Azahar needs roughly half a minute to bring up Vulkan and map its window; there is no output to watch. The script polls, restarts once if no window appears, and captures that window's rectangle.
- **The screen must be unlocked.** With the session at the login window the emulator still runs and still renders — its log shows Vulkan up and the game executing — but it maps no window, so there is nothing to capture. The script checks for this and fails immediately instead of waiting out its timeout.

`Screen Recording` permission is required for `screencapture`, and `Accessibility` for the window lookup; grant both to the terminal running it. Captures are Retina, so a 1706x752 window yields a 3412x1504 PNG.

### Host snapshots

`make snapshots` replays the same display lists the 3DS draws into PNGs without a GPU. Glyphs come from a stand-in font, so text widths are approximate; the emulator is the pixel reference.

### Fonts

There is no bundled font. Indigo draws text with citro2d's default, the 3DS system font (`src/ui/ui.c` creates the text buffer with `C2D_TextBufNew`). citro2d rasterises glyphs into VRAM on demand as text is parsed, so nothing needs preloading, and the buffer holds 4096 glyphs against a worst case of about 500 for the fullest screen.

This is a deliberate choice rather than an omission: post text, display names and biographies are arbitrary Unicode, and the system font is what covers it. A bundled font would trade that coverage for byte-identical text between emulator and hardware, and would carry a licence obligation. If fixed UI labels are ever worth that trade, the font can be loaded from ROMFS at startup with `C2D_FontLoad` and selected per text buffer.

### Releases

`scripts/release.sh [--dry-run] <version>` tags, builds and publishes a GitHub release with the `.3dsx`. It refuses to run unless you are on a clean `main` that equals `origin/main`, and it needs a matching `CHANGELOG.md` section.

The devkitPro `3ds_rules` infrastructure produces the `.3dsx` executable and embedded SMDH metadata. Indigo is intended to be launched through the Homebrew Menu.

## Installing

The resulting application is intended for:

```
sdmc:/3ds/indigo/indigo.3dsx
```

This is an application bundle layout understood by the 3DS Homebrew Menu. A CIA target is not part of the current project scope.

For development, the Homebrew Menu's 3dslink/netloader path can also be used when the console and development machine have working network connectivity.

## Architecture

```
src/
├── main.c        libctru lifecycle and frame loop
├── app/          application state and navigation
├── gfx/          platform-neutral display lists (canvas)
├── ui/           layout (pure) and citro2d/citro3d backend
├── input/        buttons, sticks and touchscreen
├── atproto/      Wolfram-backed protocol integration
└── util/         logging and small helpers
```

The renderer is GPU-backed rather than console-only. The current UI is still intentionally a shell: it establishes the two-screen rendering model and exercises the actual 3DS input devices without pretending that the Bluesky client exists yet.

The top screen is the primary reading surface. The bottom screen is the interaction surface. Touch is an enhancement; important actions must remain usable through physical controls. The 3DS touchscreen can only reach the bottom screen, which is why nothing is opened by tapping the picture itself: every control is on the interaction surface, and where a button has no key left for it -- the viewer, on a screen whose twelve buttons are spoken for -- it takes a New 3DS one and the touchscreen covers the rest.

## 3DS platform model

Indigo is a normal 3DS homebrew application, not a desktop application wrapped for the console.

Platform code should use libctru for:

- application lifecycle;
- HID;
- graphics/system services;
- SDMC storage;
- 3DS-specific facilities.

citro3d/citro2d provide the GPU-backed rendering layer. Wolfram provides the protocol implementation and, once integrated, the transport stack.

Do not introduce SDL merely to make Indigo resemble Cobalt. A 3DS-specific application is the point.

## Cobalt relationship

Cobalt and Indigo share the same broad goal: putting native Bluesky clients on hardware that was never designed for Bluesky.

They do not share an application implementation.

Cobalt is built around Wii U-specific facilities such as WUT, SDL2, the GamePad and Aroma. Indigo instead uses libctru, citro2d/citro3d, the 3DS's two physical screens, buttons, Circle Pad and touchscreen.

Wolfram is the shared protocol layer between them because the protocol layer should not need to know which Nintendo console is running the client.

## Contributing

Changes go through a branch and a pull request, and `main` is never pushed to directly. The flow, and the checks that enforce it, are in [CONTRIBUTING.md](CONTRIBUTING.md).

## Licence

Indigo is licensed under the GNU Affero General Public License v3.0. See [LICENSE](LICENSE).
