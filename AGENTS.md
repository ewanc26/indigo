# Indigo development notes

Indigo is a native Nintendo 3DS application. It is the 3DS counterpart to Cobalt, not a Cobalt port.

## Platform

Use libctru directly for 3DS-specific functionality.

- `aptMainLoop()` owns the application loop.
- `gfxInitDefault()` / `gfxExit()` own graphics.
- `hidScanInput()` reads buttons and Circle Pad state.
- `hidTouchRead()` reads the bottom-screen touchscreen.
- Keep top and bottom screens conceptually separate.
- Do not introduce SDL solely to share code with Cobalt.

## Architecture

Keep platform concerns behind small modules:

- `app/` owns application state and screen selection.
- `ui/` owns presentation.
- `input/` owns libctru input translation.
- `net/` owns the 3DS network lifecycle.
- `atproto/` owns the boundary between Indigo and Wolfram.
- `util/` owns logging and small cross-cutting helpers.

Wolfram is the AT Protocol implementation. Do not create a second protocol stack inside Indigo.

## Two-screen model

The 3DS screens are not treated as two arbitrary windows.

The top screen is the primary reading surface. The bottom screen is the interaction surface and should make useful use of touch where appropriate.

Avoid assuming that every Cobalt screen has a one-to-one Indigo equivalent.

## Input

Translate physical input into Indigo-level actions in `input/`. Application code should not depend directly on `KEY_A`, `KEY_B`, `KEY_TOUCH` and similar libctru constants.

## Network

Keep network setup and teardown explicit. The 3DS network stack is a finite console resource; initialise it once and release it once.

Wolfram handles AT Protocol transport and cryptography. Indigo should only manage the platform prerequisites and application-facing session state.

## Current scope

The repository is intentionally a scaffold. Do not pretend unfinished protocol or UI functionality exists.

The first useful milestone is a bootable native shell with working input, screen rendering and platform lifecycle handling.
