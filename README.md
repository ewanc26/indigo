# Indigo

A native Bluesky client for the Nintendo 3DS.

**Version 0.1.0**

Indigo is the 3DS counterpart to [Cobalt](https://github.com/ewanc26/cobalt), but it is **not a port** of Cobalt. I am designing it around the 3DS's own hardware, input model and two-screen layout.

The project uses [Wolfram](https://github.com/ewanc26/wolfram), my C AT Protocol SDK, for protocol functionality.

## Status

**Native 3DS shell.**

The repository now has a proper devkitARM/libctru application lifecycle, GPU-backed citro2d rendering, native 3DS input handling and the Wolfram integration boundary. Bluesky session and feed functionality are not implemented yet.

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

cd ../wolfram
cmake -S . -B build-3ds \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/.devdeps/3ds.cmake \
  -DWOLFRAM_BUILD_3DS=ON \
  -DWOLFRAM_BUILD_TESTS=OFF \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-3ds

cd ../indigo
make
```

Indigo looks for Wolfram at `../wolfram/build-3ds` by default. Set `WOLFRAM_ROOT` and `WOLFRAM_BUILD` to override those paths.

### Build targets

| Command | Purpose |
|---|---|
| `make` | Build the 3DS application |
| `make run` | Print the SD-card installation path |
| `make clean` | Remove generated build output |

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
├── ui/           citro2d/citro3d rendering
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

citro3d/citro2d provide the GPU-backed rendering layer. Wolfram provides the AT Protocol implementation and, once integrated, the transport stack.

Do not introduce SDL merely to make Indigo resemble Cobalt. A 3DS-specific application is the point.

## Cobalt relationship

Cobalt and Indigo share the same broad goal: putting native AT Protocol clients on hardware that was never designed for Bluesky.

They do not share an application implementation.

Cobalt is built around Wii U-specific facilities such as WUT, SDL2, the GamePad and Aroma. Indigo instead uses libctru, citro2d/citro3d, the 3DS's two physical screens, buttons, Circle Pad and touchscreen.

Wolfram is the shared protocol layer between them because AT Protocol should not need to know which Nintendo console is running the client.

## Licence

Indigo is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE).
