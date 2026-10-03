# Changelog

All notable changes to Indigo are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). `scripts/release.sh`
publishes the section matching the version you pass it.

## [Unreleased]

### Added
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
- Requires the Wolfram fix aligning its `soc_ctx` buffer to a page; without it
  Azahar aborts with a kernel assertion when `wf_platform_init` runs.

### Changed
- The 3DS renderer now replays display lists instead of drawing directly, and
  clears the text buffer once per screen (it was cleared per string).
- The Wolfram adapter initialises Wolfram's 3DS platform layer and proves the
  link with a pure library call.
