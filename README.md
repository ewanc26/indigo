# Indigo

A native Bluesky client for the Nintendo 3DS.

**Version 0.1.0**

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

**Milestone 4 (threads, profiles, notifications, More menu, compose) is ready for review. It is stacked on Milestone 3, which is stacked on Milestone 2, which is stacked on Milestone 1.** Milestones 1 to 3 are open as pull requests #1, #2 and #3 and are not merged yet.

| Check | State |
|---|---|
| Host unit tests, warnings-as-errors sweep, snapshot renderer | verified on the host (sign-in form, URL/handle normalising, session file codec and store, error mapping, log file) |
| TLS to the real service with the bundled CA roots | emulator-verified in Azahar: handshake to `https://bsky.social` succeeds and the server's rejection of a bogus login shows as "Wrong handle or app password." ([screenshot](docs/screenshots/m2-azahar-signin-error.png)) |
| Sign-in to a custom PDS (eurosky.social), session save, resume on restart, sign-out | emulator-verified in Azahar: signed in, session saved, relaunch resumed it without credentials, Sign out deleted the session file and returned to the sign-in screen ([signed out](docs/screenshots/m3-emulator-signed-out.png)) |
| On-screen keyboard (swkbd) entry | **not verified** in the emulator (credentials were supplied by the dev autofill file) |
| Timeline model, wrapping, link spans, selection, paging triggers, like/repost state | verified on the host (684 checks); both screens rendered by the host snapshot renderer ([top and bottom](docs/screenshots/m3-snapshot-timeline.png), [scrolled](docs/screenshots/m3-snapshot-timeline-scrolled.png)), which are **not emulator output** |
| M3 build boots in Azahar | emulator-verified: the sign-in screen still renders ([screenshot](docs/screenshots/m3-azahar-boot.png)) |
| Timeline fetch, paging, reload, like, unlike, repost, unrepost against the real service | emulator-verified in Azahar on the live account: first page, a second page (26 posts, [screenshot](docs/screenshots/m3-emulator-paging.png)), reload, and like/unlike and repost/unrepost with the counts moving by one each way ([liked](docs/screenshots/m3-emulator-liked.png)); every like and repost was undone afterwards |
| Thread fetch, profile fetch, notifications fetch, compose of a post, a reply and a quote | emulator-verified in Azahar against the live account: `indigo.log` on the virtual SD records `thread: 2 posts`, `notifications: 30` and `published (mode 0)`, `(mode 1)` and `(mode 2)` |
| More menu built from the post being read, with its facet targets, scrolling, and choosing a mention to open that profile | verified on the host (1045 checks, including label text, payload, scrolling bounds, out-of-range facet ranges and the touch/action mapping); both screens rendered by the host snapshot renderer ([facet targets](docs/screenshots/m4-snapshot-menu-facets.png), [scrolled](docs/screenshots/m4-snapshot-menu-facets-scrolled.png)), which are **not emulator output**. The menu itself was opened in the emulator and both screens rendered ([more menu](docs/screenshots/m4-azahar-more-menu.png)), though the capture cannot be read back here to confirm which post's targets it listed |
| M4 build boots in Azahar and renders both screens | emulator-verified: `make run-emu` builds, launches, resumes the saved session and fetches a page; the top screen shows the selected post and the bottom screen the touch list ([both screens](docs/screenshots/m4-azahar-home.png)) |
| Every control is hinted exactly once and no text runs off a screen | verified on the host: a test walks both display lists for every screen and fails if a button glyph is hinted on both displays or if any text exceeds its canvas |
| Avatars | **not implemented** (deferred: no image decoder yet) |
| Real hardware | never run |

Sign-in takes a service URL (default `https://bsky.social`), a handle and an app password, and follows the account's PDS. The session is saved to `sdmc:/3ds/indigo/session.dat` as plaintext: the SD card has no permissions, and an obfuscation key stored beside the file would be false comfort. Use an app password, never your main password. Logs go to `sdmc:/3ds/indigo/indigo.log` and never contain tokens or passwords.

Development and verification happen on an emulator first; nothing here has been tested on a real 3DS. Sound, sleep, the HOME menu and the icon and banner are the hardware pass (Milestone 5) and are untouched.

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
| `make test` | Host unit tests (ASan + UBSan, warnings are errors); no devkitARM needed |
| `make warnings` | Warnings-as-errors sweep of every host-portable source |
| `make snapshots` | Render PNGs of both screens to `build-host/snapshots/` |
| `make clean` | Remove generated build output |

### Emulator

```sh
make run-emu                                  # Azahar at ~/Applications/Azahar.app
make run-emu EMU=/path/to/citra               # Citra or Lime3DS as fallbacks
```

Azahar keeps its virtual SD card at `~/Library/Application Support/Azahar/sdmc` on macOS (the `sdmc:` root; Indigo writes `3ds/indigo/indigo.log` and `session.dat` there). Its own log, including the font warning below, is at `~/Library/Application Support/Azahar/log/azahar_log.txt`.

Emulator results are emulator-verified only. TLS, certificates and DNS can behave differently from hardware, and emulator performance must never drive tuning. The 3D slider, sleep and the Home menu are left to the hardware pass.

One font caveat is worth knowing before trusting any screenshot: Azahar has no 3DS system shared font and logs `Shared Font file missing. Loading open source replacement from memory` on every launch. Indigo draws with the 3DS system font, so glyph coverage and advance widths in an emulator capture are Azahar's substitute, not the console's. Layout and behaviour can be checked in the emulator; text metrics cannot.

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

The top screen is the primary reading surface. The bottom screen is the interaction surface. Touch is an enhancement; important actions must remain usable through physical controls.

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

## Licence

Indigo is licensed under the GNU Affero General Public License v3.0. See [LICENSE](LICENSE).
