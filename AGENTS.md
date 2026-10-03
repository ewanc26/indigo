# AGENTS.md — Indigo

Indigo is a native Nintendo 3DS homebrew client for Bluesky. It is scoped to the Bluesky app: it is not a general AT Protocol client and speaks Bluesky's own lexicons (`app.bsky.*`). Accounts on any PDS must work, so sign-in takes a service URL (default `https://bsky.social`) and the session follows the account's own PDS.

It is the 3DS counterpart to Cobalt, but it is **not a port of Cobalt**. The application, rendering, interaction model and platform integration must be designed for the 3DS.

This document is the architectural contract for coding agents working in the repository. Keep it aligned with the code.

## 1. Platform and project intent

Indigo is a normal 3DS homebrew application targeting the current devkitPro ecosystem:

- devkitARM;
- libctru;
- citro3d;
- citro2d;
- devkitPro 3DS portlibs where required;
- the standard devkitARM `3ds_rules` Makefile infrastructure;
- `.3dsx` as the development/homebrew executable format.

The normal launch environment is the 3DS Homebrew Menu, typically through Luma3DS/Rosalina.

The project should feel native to the console rather than like a desktop Bluesky client compressed onto two screens.

The immediate milestone is a real native 3DS shell. The current shell establishes:

- libctru application lifecycle;
- GPU-backed citro2d rendering;
- separate top and bottom render targets;
- buttons, Circle Pad, C-Stick and touchscreen input;
- the Wolfram protocol boundary.

Bluesky session/feed functionality comes later.

## 2. Relationship to Cobalt

Cobalt is the Wii U client. Indigo is the 3DS client.

They share product intent and protocol concepts, but not their platform implementation.

Do not copy Cobalt's:

- rendering code;
- SDL2 usage;
- Wii U lifecycle;
- GamePad assumptions;
- screen layout;
- filesystem conventions;
- platform networking.

When bringing a feature from Cobalt to Indigo, reproduce the behaviour only after deciding what the correct 3DS interaction is.

## 3. Wolfram is the protocol layer

Wolfram is my C AT Protocol SDK and is the intended protocol implementation for Indigo.

Do not implement another AT Protocol stack in Indigo.

Protocol concerns belong in Wolfram, including:

- XRPC;
- Lexicon types;
- identity/DID handling;
- repository operations;
- AT URIs;
- session primitives;
- protocol parsing;
- cryptography;
- pagination primitives;
- protocol-level error handling;
- HTTP/TLS transport.

Indigo's `src/atproto/` module is an application-facing adapter. It should translate between Indigo's application state and Wolfram's API.

Wolfram already has a 3DS platform implementation. Its platform layer owns the libctru socket service and other platform primitives. Therefore Indigo must not independently call `socInit()` and then ask Wolfram to initialise the same service.

The old standalone `src/net/` scaffold has been removed for this reason. If another platform lifecycle boundary becomes necessary, add it only with a clearly defined owner.

Before adding a dependency to Indigo, check whether Wolfram already provides the required functionality.

## 4. 3DS application lifecycle

The normal lifecycle is:

1. `gfxInitDefault()`;
2. initialise the GPU/rendering layer;
3. initialise application state;
4. enter `aptMainLoop()`;
5. call `hidScanInput()` once per frame;
6. translate input;
7. update application state;
8. render;
9. wait for VBlank;
10. shut down subsystems in reverse ownership order;
11. `gfxExit()`.

The current renderer uses citro3d/citro2d. citro2d sits on top of citro3d and provides the 2D drawing and system-font text facilities used by the shell.

Do not block the render loop on network requests.

START should continue to provide a predictable exit path back to the homebrew launcher.

Do not claim that the application has been hardware-tested unless it has actually been run on a 3DS.

## 5. Graphics

The current renderer is GPU-backed.

The intended stack is:

```
libctru
  └── citro3d
       └── citro2d
            └── Indigo UI
```

The official devkitPro 3DS examples cover citro2d/citro3d rendering, input, networking, SDMC, ROMFS and threading. Use them as platform references rather than inventing incompatible lifecycle patterns.

The renderer owns:

- C3D initialisation/finalisation;
- C2D initialisation/finalisation;
- top-screen render target;
- bottom-screen render target;
- text buffers;
- future textures/sprites/fonts.

Application code must not depend on `C2D_RenderTarget` details.

The current UI is intentionally simple. It is a real renderer, not the final client design.

When adding graphics:

- keep allocations bounded;
- avoid recreating GPU resources every frame;
- separate layout from drawing;
- do not retain unnecessary decoded images;
- use the system font initially where appropriate;
- introduce custom fonts only with an explicit Unicode/layout reason.

## 6. Two-screen design

Treat the two displays as different surfaces.

### Top screen

The top screen is the primary reading surface.

Use it for:

- timelines;
- posts;
- threads;
- profiles;
- media;
- other information-heavy content.

### Bottom screen

The bottom screen is the interaction surface.

Use it for:

- navigation;
- actions;
- filters;
- compose controls;
- contextual controls;
- touch interaction.

Do not mirror the top screen onto the bottom screen.

The touchscreen is an additional input method, not the only interaction path.

## 7. Input

libctru exposes the actual 3DS input hardware. The Indigo input abstraction currently tracks:

- held keys;
- pressed keys;
- released keys;
- Circle Pad position;
- C-Stick position;
- touchscreen coordinates;
- touch down/press/release.

Keep raw libctru key constants inside `input/` where possible.

Core navigation must work on Old 3DS hardware. New 3DS-only inputs such as C-Stick and ZL/ZR can enhance the experience but cannot be required for basic operation.

When adding a gesture or touchscreen-only interaction, provide a physical-control equivalent when the operation is important.

Do not make assumptions based on Cobalt's GamePad input model.

## 8. Input polling order

A frame should poll HID once:

```c
hidScanInput();
indigo_input_begin_frame(&input);
indigo_input_poll(&input);
```

Do not call `hidScanInput()` from several modules.

Application code consumes Indigo input state rather than directly polling libctru.

## 9. Text and Unicode

AT Protocol content is arbitrary Unicode.

Do not assume posts, display names, handles, biographies or alt text are ASCII.

The citro2d system font is useful for the bootstrap shell, but it is not a guarantee that every Unicode character a server can return will have a glyph.

The eventual text system should account for:

- UTF-8;
- emoji;
- accented Latin;
- Greek/Cyrillic;
- CJK where supported;
- combining marks;
- long strings;
- missing glyphs;
- text wrapping.

Do not use byte length as rendered width.

Keep measurement/layout separate from drawing so feed items can calculate their height consistently.

## 10. Application architecture

Current source boundaries are:

```
src/
├── main.c        libctru lifecycle and frame loop
├── app/          application state and navigation
├── gfx/          platform-neutral display lists (canvas)
├── ui/           layout.c (pure) and ui.c (citro2d/citro3d backend)
├── input/        3DS buttons, sticks and touchscreen
├── atproto/      Wolfram-backed protocol integration
└── util/         logging and small helpers
```

Keep modules responsibility-focused.

Avoid turning `app.c` or `ui.c` into a catch-all.

The flat VPATH used by the devkitPro Makefile means same-named source files can become ambiguous object names. Prefer descriptive filenames when two directories would otherwise both contain something such as `profile.c`.

## 11. Bluesky application surface

The initial useful client surface should be built incrementally:

1. session/login;
2. home timeline;
3. profile viewing;
4. post/thread viewing;
5. compose;
6. replies;
7. likes/reposts;
8. notifications;
9. actor search;
10. account/session management.

Do not implement anything beyond the Bluesky app (other AT Protocol apps, custom lexicons, PDS administration). Custom PDS hosting for a Bluesky account is in scope.

UI modules must not manually construct XRPC requests.

Feeds must be paginated and bounded. Do not keep an unbounded timeline in memory.

Network failures, expired sessions and malformed responses must become application states rather than crashes.

Do not assume that every account uses bsky.social.

## 12. Authentication

Never hard-code or commit:

- account passwords;
- app passwords;
- access tokens;
- refresh tokens;
- DPoP keys;
- OAuth secrets.

Use Wolfram's authentication/session functionality where available.

The first usable milestone does not require OAuth. If OAuth is added later, treat it as a dedicated architecture change.

If credentials are persisted to SDMC:

- keep them under an Indigo-specific directory;
- store the minimum necessary data;
- provide sign-out;
- do not print secrets in logs;
- treat corrupt session files as recoverable;
- do not silently replace user data.

## 13. Networking

Networking is owned by Wolfram once the protocol layer is connected.

Wolfram's existing 3DS implementation uses libctru's socket layer and is designed to work with the 3DS curl/portlib environment.

Indigo must not add another HTTP/TLS/socket stack merely because those libraries are available.

Network operations must be resilient to:

- Wi-Fi loss;
- DNS failure;
- TLS failure;
- HTTP errors;
- timeouts;
- rate limits;
- unavailable PDS/AppView services.

Never spin indefinitely waiting for the network.

Long operations should be represented as jobs/state transitions rather than hidden inside rendering.

## 14. Storage

Use SDMC for persistent application state when required.

Keep Indigo data in an application-specific directory.

Potential state includes:

- settings;
- session state;
- cached data;
- drafts;
- explicit offline queues;
- optional diagnostic logs.

Persistent formats must tolerate missing, empty, truncated and malformed files.

Version any format that is likely to survive an application update.

Never silently discard a user's draft.

Do not make cached data a prerequisite for booting.

## 15. Offline behaviour

Separate:

- remote authoritative state;
- local cache;
- optimistic UI state;
- pending mutations.

Do not create an implicit queue that publishes old actions without the user understanding that they were queued.

If an offline compose queue is added, provide explicit inspection, retry and discard controls.

## 16. Accessibility

There is no assumption that a custom Indigo UI will automatically inherit a modern desktop accessibility layer.

Design for:

- readable text;
- strong focus indication;
- predictable navigation;
- physical-button alternatives;
- meaningful labels;
- image alt text where available;
- no colour-only status indicators;
- sufficiently large touch targets;
- restrained animation.

Do not sacrifice basic readability to increase feed density.

## 17. Performance and memory

Treat the 3DS as constrained hardware.

Be deliberate with:

- heap allocations;
- large JSON responses;
- image decoding;
- glyph storage;
- duplicate strings;
- feed caches.

Do not retain complete network responses when only a small subset is needed.

Use bounded collections for timelines and notifications.

Do not decode and retain every image in a feed simultaneously.

Measure before introducing complicated optimisation, but avoid obviously unbounded structures.

## 18. Threading

Threads are optional, not mandatory.

If background work is introduced:

- define ownership of shared state;
- keep rendering on the main thread;
- synchronise shared queues;
- make shutdown deterministic;
- ensure worker threads cannot outlive referenced objects.

Prefer Wolfram's existing 3DS platform primitives over introducing another threading abstraction.

## 19. Homebrew packaging and distribution

The normal development output is a `.3dsx` with embedded SMDH metadata.

The application bundle layout is:

```
sdmc:/3ds/indigo/indigo.3dsx
```

The Homebrew Menu recognises this as an application bundle and uses embedded SMDH metadata for the displayed name, description and icon when present.

Do not add a CIA target merely because `.cia` files exist in the 3DS ecosystem. It is outside the current scope.

For development, 3dslink/netloader is a useful alternative to repeatedly removing the SD card. Do not hot-swap the SD card while homebrew is running.

If an icon is added, wire it through the standard `APP_ICON`/SMDH path supplied by `3ds_rules` rather than inventing an application-specific packaging format.

## 20. Debugging

Luma3DS/Rosalina is the preferred modern homebrew launch environment and provides facilities useful for development, including remote debugging.

Keep diagnostics useful without leaking credentials.

The logging module may remain lightweight. Do not dump authenticated request headers, tokens or session objects.

A debug build can be more verbose than a release build.

## 21. Build system

The Makefile intentionally follows the current devkitPro 3DS application template.

Important assumptions:

- `DEVKITARM` points at devkitARM;
- `DEVKITPRO` points at the devkitPro installation;
- `3ds_rules` supplies the standard recursive build/package rules;
- ARMv6K is the target architecture;
- citro2d/citro3d come from the 3DS development environment;
- Wolfram is separately built for 3DS.

The normal build sequence is:

```sh
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

Do not claim a build succeeded without actually running it.

Do not silently substitute a host compiler or desktop libraries.

## 22. Testing

Layout is pure: `ui/layout.c` fills `gfx/canvas` display lists and never calls libctru or citro2d, so `make test`, `make warnings` and `make snapshots` run on the host without devkitARM. `ui/ui.c` only replays display lists. Keep new screens in layout so they can be snapshotted; UI work needs a snapshot or emulator screenshot of both screens.

Validation has distinct levels.

### Source/build validation

Run the relevant Makefile target with the intended devkitPro environment.

### Hardware validation

A successful cross-build does not prove hardware behaviour.

When hardware is available, test:

- launch through Homebrew Menu;
- both screens;
- physical buttons;
- Circle Pad;
- C-Stick where applicable;
- touchscreen;
- START exit;
- SDMC access;
- network connectivity;
- protocol/session behaviour;
- long-running feed scrolling;
- clean shutdown.

Do not call emulator or host testing hardware testing.

### Regression validation

When changing Wolfram's 3DS integration, build Wolfram's 3DS target as well as Indigo.

## 23. Common mistakes

Watch for:

- missing devkitARM/devkitPro environment;
- missing citro2d/citro3d;
- missing 3DS portlibs;
- Wolfram built for the host instead of ARM;
- wrong sibling Wolfram path;
- duplicate socket initialisation;
- desktop libraries accidentally entering the link;
- flat-VPATH object collisions;
- C/C++ linker selection errors;
- New 3DS-only controls becoming mandatory;
- unbounded feed/image allocations;
- blocking network requests on the render thread;
- treating a cross-build as proof of hardware compatibility.

## 24. Development phases

The intended progression is:

### Phase 1 — native shell

- libctru lifecycle;
- citro2d/citro3d renderer;
- two-screen layout;
- complete input abstraction.

### Phase 2 — application state

- navigation;
- focus model;
- scrolling;
- screen transitions;
- input routing.

### Phase 3 — Wolfram integration

- resolve the actual Indigo/Wolfram session boundary;
- remove remaining scaffold assumptions;
- connect network lifecycle through Wolfram;
- session/authentication state.

### Phase 4 — first Bluesky surface

- timeline;
- post rendering;
- pagination;
- profile;
- threads.

### Phase 5 — interaction

- compose;
- replies;
- likes/reposts;
- notifications;
- search.

### Phase 6 — persistence and polish

- settings;
- session persistence;
- cache;
- drafts;
- media;
- accessibility;
- performance;
- distribution metadata.

Do not skip the platform foundations by implementing a web-client-shaped UI first.

## 25. Code conventions

Use the existing C style:

- C unless C++ is justified;
- small structs;
- explicit ownership;
- init/shutdown pairs;
- `bool` for boolean state;
- descriptive `indigo_*` names;
- narrow public headers;
- platform includes kept in platform-facing modules where practical.

Comments should explain decisions and hardware constraints rather than restating the code.

Do not introduce SDL merely for API familiarity.

## 26. Security

Never commit credentials or private key material.

Never log:

- access tokens;
- refresh tokens;
- app passwords;
- DPoP keys;
- OAuth secrets;
- authenticated cookies/headers.

Use Wolfram's existing cryptographic implementation.

## 27. Agent workflow

Before editing:

1. Read this file.
2. Read the README.
3. Inspect the relevant source and callers.
4. Inspect Wolfram if the change crosses the protocol boundary.
5. Inspect the Makefile if build inputs change.
6. Check Cobalt for product-level behaviour only.
7. Keep the change scoped.

After editing:

- run the build when the toolchain is available;
- run relevant tests;
- inspect the diff;
- update AGENTS.md when architecture changes;
- never claim checks that were not run.

Prefer focused commits.

## 28. Current state

The current repository has:

- a standard libctru/devkitARM application entry point;
- GPU-backed citro2d rendering on both physical screens;
- native buttons, Circle Pad, C-Stick and touchscreen polling;
- a small application/navigation state machine;
- a Wolfram adapter boundary;
- no Bluesky session/feed implementation yet.

The renderer and input system are now real 3DS homebrew foundations rather than console-text-only placeholders.

The next major architectural step is wiring the adapter to Wolfram's real 3DS session/transport API without reintroducing duplicate socket ownership.

Keep this document current whenever those boundaries change.
