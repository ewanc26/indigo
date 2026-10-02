# Indigo

A native AT Protocol / Bluesky client for the Nintendo 3DS.

**Version 0.1.0**

Indigo is the 3DS counterpart to [Cobalt](https://github.com/ewanc26/cobalt), but it is **not a port** of Cobalt. It is being designed around the 3DS's own hardware, input model and two-screen layout rather than trying to reproduce the Wii U implementation on different hardware.

The project uses [Wolfram](https://github.com/ewanc26/wolfram), Ewan's C AT Protocol SDK, for protocol functionality.

## Status

**Scaffolding.**

The repository currently contains the native 3DS application shell, build system and platform abstractions. Bluesky functionality is not implemented yet.

The first milestone is a small, bootable application that establishes the native rendering, input, networking and Wolfram integration layers before the higher-level Bluesky UI is built.

## Requirements

Indigo targets real Nintendo 3DS hardware and the standard homebrew development environment.

Building requires:

- devkitPro with devkitARM and libctru
- 3DS portlibs, including curl, mbedTLS and zlib
- a sibling checkout of [Wolfram](https://github.com/ewanc26/wolfram) built for 3DS

Install the platform dependencies with devkitPro's package manager as required by the local toolchain.

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
| `make run` | Print the expected SD-card location |
| `make clean` | Remove generated build output |

## Installing

The resulting application is intended for:

```
sd:/3ds/indigo/indigo.3dsx
```

A CIA target can be added once the application reaches a stable enough state to justify packaging it.

## Architecture

```
src/
├── main.c        entry point and native 3DS frame loop
├── app/          application state and screens
├── ui/           top/bottom-screen presentation
├── input/        buttons, Circle Pad and touch
├── net/          3DS network lifecycle
├── atproto/      Wolfram-backed protocol integration
└── util/         logging and shared platform utilities
```

The 3DS-specific layers deliberately use libctru directly. Indigo does not introduce SDL merely to make the code resemble Cobalt.

## Cobalt relationship

Cobalt and Indigo share the same broad goal: putting a native AT Protocol client on hardware that was never designed for Bluesky.

They do not share an application implementation.

Cobalt is built around Wii U-specific facilities such as WUT, SDL2, the GamePad and Aroma. Indigo instead uses libctru, the 3DS's two physical screens, buttons, Circle Pad and touchscreen.

Wolfram is the shared layer between them because AT Protocol itself should not need to know which Nintendo console is running the client.

## Licence

Indigo is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE).
