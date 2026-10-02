# AGENTS.md — Indigo

Indigo is a native Nintendo 3DS homebrew client for the AT Protocol / Bluesky.

It is the 3DS counterpart to Cobalt, but it is **not a port of Cobalt**. The application, interaction model, rendering strategy and platform integration must be designed for the 3DS rather than treating the Wii U implementation as a template to copy line-for-line.

This file is the architectural contract for coding agents working in the repository. Read it before making changes. If the code and this document disagree, inspect the code and update this document when the intended architecture has changed rather than allowing the two to drift apart.

---

## 1. Project intent

Indigo exists to put a native AT Protocol client on Nintendo 3DS hardware.

The interesting part is not reproducing a modern Bluesky web client on a tiny screen. The interesting part is making the application feel like a 3DS application while still exposing the useful parts of Bluesky:

- reading timelines and threads;
- viewing profiles;
- searching;
- notifications;
- composing posts and replies;
- interacting with posts;
- handling the account/session lifecycle;
- eventually supporting the parts of the AT Protocol surface that make sense on this hardware.

The project should take the 3DS seriously as a platform:

- one physical bottom touchscreen;
- one physical top screen;
- buttons and Circle Pad on every model;
- C-Stick and ZL/ZR on New 3DS hardware;
- limited CPU, memory and rendering bandwidth compared with current phones and desktops;
- an ARM11/libctru execution environment;
- an SD-card filesystem rather than a conventional desktop application data directory;
- network connectivity that can disappear at any time.

Do not optimise for feature count at the expense of making the application usable on the console.

The first milestone is a genuinely bootable native shell. Later milestones should add protocol and UI functionality incrementally.

---

## 2. Relationship to Cobalt

Cobalt and Indigo share a goal, not an implementation.

Cobalt is the Wii U client. Indigo is the 3DS client. They should share protocol concepts through Wolfram where practical, but platform code should remain platform-specific.

Do not copy Cobalt's:

- rendering code;
- input code;
- Wii U lifecycle code;
- screen layout assumptions;
- SDL-specific abstractions;
- GamePad-specific interaction model;
- Wii U filesystem conventions;
- networking implementation merely because it already exists.

Cobalt is useful as a reference for application behaviour, AT Protocol feature decisions and lessons learned. It is not an Indigo framework.

When a Cobalt feature is brought to Indigo, first decide what the equivalent interaction should be on a 3DS. A feature can have the same semantic behaviour while having completely different controls and screen composition.

---

## 3. Shared protocol layer: Wolfram

Wolfram is my C AT Protocol SDK and is the intended protocol implementation for Indigo.

Do not build a second AT Protocol stack inside Indigo.

Wolfram already has a dedicated 3DS platform implementation and a 3DS CMake toolchain. Its 3DS platform currently uses libctru's socket layer and platform primitives, including `socInit()`, LightLock and `osGetTime()`. The 3DS build is intended to use the devkitARM/libctru environment.

This has an important consequence for Indigo's current scaffold:

**Do not initialise the 3DS socket service twice.**

The current `src/net/` scaffold predates the Wolfram 3DS integration being wired through. Before the real Wolfram session is connected, reconcile `indigo_net_init()` with Wolfram's `wf_platform_init()` rather than allowing both layers to call `socInit()`. There should ultimately be one clear owner for the platform network lifecycle.

Similarly, do not add a second HTTP/TLS stack merely because Indigo has a `net/` directory. If Wolfram provides the transport, Indigo should consume it.

When Indigo needs protocol functionality that Wolfram does not expose:

1. Check whether the functionality already exists elsewhere in Wolfram.
2. Extend Wolfram if the missing operation belongs to the SDK.
3. Keep Indigo-side code focused on presentation, application state and console-specific behaviour.
4. Do not fork Wolfram logic into Indigo as a shortcut.

Wolfram is also the correct place for protocol-level details such as:

- XRPC request construction;
- Lexicon types;
- session handling;
- identity resolution;
- repository operations;
- AT URI/DID handling;
- record encoding;
- protocol-specific error handling;
- cryptography;
- pagination primitives;
- protocol response parsing.

Indigo should receive application-friendly data and status from that layer.

---

## 4. Target platform and toolchain

Indigo targets Nintendo 3DS homebrew using:

- devkitPro;
- devkitARM;
- libctru;
- the 3DS portlibs where required;
- the standard devkitPro `3ds_rules` Makefile infrastructure.

The normal dependency package is the devkitPro 3DS development environment. The exact package set should follow the installed devkitPro release rather than assuming a particular host distribution.

The architecture is ARM11. Do not introduce desktop-only assumptions into platform code.

The current Makefile uses a standard devkitARM recursive build structure. Keep one build system: do not introduce CMake solely for Indigo when the application itself is already using devkitPro Makefile rules. CMake remains appropriate for building the sibling Wolfram checkout because Wolfram's 3DS support is configured through its `.devdeps/3ds.cmake`.

The normal dependency sequence is:

1. install the 3DS devkitPro environment;
2. build Wolfram with its 3DS toolchain;
3. build Indigo with `make`;
4. copy the resulting `.3dsx` to the SD card;
5. launch it through the chosen 3DS homebrew environment.

Do not claim a build works unless the relevant toolchain has actually been run.

---

## 5. Repository structure

The current structure is intentionally small:

```
indigo/
├── AGENTS.md
├── LICENSE
├── Makefile
├── README.md
├── .gitignore
└── src/
    ├── main.c
    ├── app/
    │   ├── app.c
    │   └── app.h
    ├── atproto/
    │   ├── atproto.c
    │   └── atproto.h
    ├── input/
    │   ├── input.c
    │   └── input.h
    ├── net/
    │   ├── net.c
    │   └── net.h
    ├── ui/
    │   ├── ui.c
    │   └── ui.h
    └── util/
        ├── log.c
        └── log.h
```

Keep the boundaries meaningful:

- `main.c` — startup, shutdown and frame-loop orchestration.
- `app/` — application state, navigation and screen-level behaviour.
- `ui/` — rendering and presentation.
- `input/` — physical 3DS input translated into Indigo actions.
- `net/` — platform network lifecycle only, if it remains necessary after Wolfram integration is completed.
- `atproto/` — Indigo-facing adapter around Wolfram.
- `util/` — logging and small cross-cutting helpers.

As the application grows, add modules according to responsibility rather than allowing `app.c` or `ui.c` to become catch-all files.

Avoid filenames that collide across directories when the devkitPro Makefile's flat `VPATH` can turn them into ambiguous object names. Cobalt has already encountered this class of problem. Prefer descriptive names when two modules would otherwise both become something like `profile.o`.

---

## 6. Application lifecycle

The native application loop is based on libctru's normal 3DS lifecycle:

- `gfxInitDefault()`;
- console/rendering setup;
- `aptMainLoop()`;
- per-frame input polling;
- application update;
- rendering;
- `gfxFlushBuffers()`;
- `gfxSwapBuffers()`;
- `gspWaitForVBlank()`;
- orderly subsystem shutdown;
- `gfxExit()`.

Keep startup and shutdown ordering explicit.

Do not perform expensive network work directly in the render loop.

Do not make the application unresponsive while waiting for network requests.

The frame loop should remain deterministic enough that input, navigation and rendering remain responsive even when protocol operations are pending.

When adding asynchronous work, keep the UI state machine separate from the transport/job implementation. A network request should produce a state transition or result rather than becoming a hidden blocking operation inside a draw function.

---

## 7. Two-screen UI model

The 3DS has two different physical displays and Indigo should treat them as two surfaces with different purposes.

Do not model the screens as arbitrary desktop windows.

The default design direction is:

### Top screen

The top screen is the primary reading surface.

It should normally carry information such as:

- timeline content;
- threads;
- profiles;
- media;
- focused post content;
- larger contextual information.

The top screen has the higher-resolution visual role and should prioritise readable text and useful information density.

### Bottom screen

The bottom screen is the interaction surface.

It should make meaningful use of:

- touch;
- navigation controls;
- contextual actions;
- filters;
- compose controls;
- navigation;
- focused-item actions.

Do not simply duplicate the top screen on the bottom screen.

### Touch and buttons

Every important operation must have a sensible button-based path. Touch is an additional interaction method, not the only way to operate the client.

The application should remain usable when the user prefers physical controls.

Touch coordinates should be translated in `input/`, not scattered through application code.

Do not expose raw `KEY_A`, `KEY_B`, `KEY_TOUCH` and similar libctru constants to higher-level application logic.

---

## 8. Input model

libctru exposes the normal 3DS buttons, Circle Pad, touchscreen and New 3DS-specific controls.

The input abstraction should distinguish at least:

- held buttons;
- newly pressed buttons;
- released buttons where needed;
- Circle Pad direction/position;
- C-Stick direction/position when available;
- touchscreen position;
- touch-down state;
- touch-start state;
- touch-release state when needed.

New 3DS-only inputs such as ZL, ZR and C-Stick must not make the basic application unusable on Old 3DS hardware.

When a feature can benefit from C-Stick or ZL/ZR, treat those as enhancements to the common control model.

Do not assume that a particular controller mapping used by Cobalt makes sense here.

Navigation should be designed around the physical controls first, then enhanced with touch.

---

## 9. Rendering strategy

The current UI is deliberately a console-text scaffold. It is not the final rendering architecture.

The 3DS examples maintained by devkitPro cover libctru, citro3d and citro2d. When Indigo moves beyond the bootstrap UI, evaluate those libraries rather than immediately writing a bespoke GPU abstraction.

The likely progression is:

1. console text for bootstrap/debug output;
2. a real 2D rendering layer;
3. text layout and font rendering;
4. images and avatars;
5. feed/post layout;
6. touch hit-testing;
7. transitions and richer interaction.

Do not introduce a heavyweight abstraction merely because it exists.

If citro2d/citro3d is used, keep the rendering layer behind `ui/` so application code does not become coupled to GPU implementation details.

Avoid rendering every screen as a web-style card grid. The 3DS's fixed screens and touch interface should influence the layout.

---

## 10. Text, fonts and Unicode

AT Protocol content is Unicode. Posts, display names, biographies, handles and alt text cannot be assumed to be ASCII.

Plan for:

- emoji;
- accented Latin characters;
- Cyrillic;
- Greek;
- CJK;
- combining marks;
- right-to-left text where practical;
- long unbroken strings;
- malformed or unexpected Unicode input.

Do not assume that a single bundled font covers the entire Unicode range.

A missing glyph must degrade to a visible fallback rather than corrupting layout or crashing the application.

Text measurement must be separated from drawing. Feed/card heights should be calculated from the same layout rules used to render them so scrolling cannot drift.

Do not use byte length as a substitute for rendered width.

---

## 11. AT Protocol application layer

Indigo should initially concentrate on the core Bluesky client loop rather than attempting to implement the entire AT Protocol ecosystem.

A sensible early application surface is:

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

Later features can be added when the basic client is stable.

Protocol calls belong behind `src/atproto/` and Wolfram. UI modules should not manually construct XRPC requests.

Keep pagination explicit. A feed is not an infinite in-memory list.

Network failures, expired sessions, missing records and malformed responses must become application states rather than crashes.

Do not assume every server behaves exactly like bsky.social. Service URLs, PDS routing, DID resolution and server-provided capabilities matter.

---

## 12. Authentication and credentials

Authentication is a security-sensitive part of the project.

Do not hard-code:

- handles;
- passwords;
- app passwords;
- access tokens;
- refresh tokens;
- service credentials.

Do not commit test credentials.

Prefer the authentication facilities exposed by Wolfram rather than implementing another session system in Indigo.

The UX should make the distinction between an app password and a normal account password clear. Never encourage users to enter their normal account password when an app password is the intended credential.

If session persistence is added:

- use the SD-card application data area deliberately;
- minimise what is stored;
- provide a sign-out path;
- remove credentials from memory when practical;
- do not print tokens in logs;
- do not display secrets in diagnostics;
- handle corrupted session data as a normal recoverable condition.

OAuth is not a prerequisite for the first usable Indigo milestone. If OAuth is eventually supported, treat it as a dedicated architecture task rather than mixing browser-based authentication into the initial client shell.

---

## 13. Networking

Networking must be resilient.

The console can lose Wi-Fi, DNS can fail, the PDS can be unavailable, TLS can fail, or a request can time out.

Every network operation needs a failure path.

Do not:

- spin indefinitely while waiting for a response;
- freeze the whole UI for a network request;
- retry aggressively without a backoff policy;
- assume the network is present because the console booted successfully.

The current `src/net/` module exists as a platform boundary, but its ownership must be reconciled with Wolfram's existing 3DS platform implementation before the real protocol layer is connected.

Wolfram's current 3DS implementation uses libctru's socket service. Therefore Indigo should not independently initialise the same service and then ask Wolfram to initialise it again.

Keep platform prerequisites and protocol transport separate:

- Indigo owns application lifecycle and platform-specific prerequisites only where necessary.
- Wolfram owns AT Protocol transport and protocol-level behaviour.

Do not add curl, mbedTLS, sockets or another HTTP stack to Indigo simply because those libraries are available. First determine whether Wolfram already provides the required path.

---

## 14. Storage

The 3DS SD card is persistent but removable and can contain damaged or unexpected data.

If Indigo adds persistent state, isolate it under an application-specific directory rather than scattering files across the SD card.

Potential persistent data includes:

- session state;
- settings;
- cached profile/feed data;
- drafts;
- offline post queue;
- logs when explicitly enabled.

Do not make cached data a prerequisite for starting the application.

Every persisted format needs to tolerate:

- missing files;
- empty files;
- truncated files;
- malformed contents;
- version changes;
- SD-card removal or write failure.

If a file format is introduced, include a version field or another migration strategy before shipping it.

Do not silently overwrite user-authored drafts.

---

## 15. Caching and offline behaviour

Network-backed data should not be assumed to be available every frame.

Where caching is useful, separate:

- authoritative remote state;
- local cached state;
- optimistic UI state;
- pending mutations.

For actions such as likes or reposts, it can be reasonable to update the UI optimistically and reconcile later, but a failed mutation must eventually be visible to the application.

Offline support should be deliberate. Do not build an implicit offline queue that can publish old user actions unexpectedly.

If an offline post queue is eventually added, the UI must make queued posts explicit and provide a way to inspect, retry and discard them.

---

## 16. Accessibility and constrained hardware

The 3DS has no modern system-wide accessibility layer that Indigo can assume will read every custom UI element.

Therefore accessibility has to be designed into the client.

Priorities include:

- readable text sizes;
- strong visual focus indication;
- consistent navigation;
- button-based alternatives to touch;
- meaningful labels for icons;
- alt text for images where available;
- avoiding information conveyed only through colour;
- avoiding tiny touch targets;
- avoiding unnecessary animation;
- keeping interaction predictable.

Do not sacrifice basic readability to fit more posts on screen.

The application should respect the hardware's limitations rather than imitating a desktop social-media dashboard.

---

## 17. Error handling and diagnostics

Errors should be structured enough that the application can distinguish:

- no network;
- DNS failure;
- TLS failure;
- HTTP failure;
- authentication failure;
- expired session;
- invalid server response;
- rate limiting;
- malformed local state;
- unsupported protocol feature;
- internal application failure.

User-facing errors should be understandable without exposing raw implementation details.

Logs are for diagnostics. Never log credentials, session tokens, cookies, or full authenticated request headers.

The current `util/log` layer is intentionally minimal. It can grow as diagnostics become necessary, but logging must remain cheap enough for hardware.

A debug build may be more verbose than a release build.

---

## 18. Memory and performance

The 3DS is an embedded console, not a desktop machine.

Be deliberate with:

- heap allocations;
- large JSON buffers;
- decoded images;
- cached posts;
- font glyph data;
- duplicated strings;
- network response bodies.

Do not retain entire network responses after extracting the information needed by the UI unless there is a concrete reason.

Prefer bounded collections for scrolling content.

Large images should not remain decoded indefinitely.

When implementing feeds, avoid an architecture that requires the entire timeline, every avatar and every image to remain resident simultaneously.

Measure before introducing complicated optimisations, but do not ignore obvious unbounded growth.

---

## 19. Threading and asynchronous work

The application does not need a thread for every operation.

Use background work only when it improves responsiveness or is required by the underlying SDK.

If threading is introduced:

- define ownership of every shared object;
- define which thread owns UI state;
- do not touch rendering state from worker threads;
- synchronise shared queues;
- make shutdown deterministic;
- ensure worker threads cannot outlive the objects they reference.

Wolfram's 3DS platform already provides its own platform primitives. Do not invent another mutex abstraction inside Indigo unless the application actually needs one.

The main UI/render thread should remain the owner of presentation state.

---

## 20. Media and images

Bluesky content can contain images and other embeds.

Do not make media decoding part of the first boot milestone.

When media support is added:

- fetch asynchronously;
- validate response size/type;
- decode off the critical render path;
- cap decoded dimensions and memory use;
- provide placeholders while loading;
- provide a failure state;
- preserve alt text;
- avoid retaining every decoded image forever.

Do not assume every embed is an image. AT Protocol records can contain different embed types and new types can appear.

Unknown embeds should degrade gracefully.

---

## 21. UI state and navigation

Keep navigation explicit.

A screen should know:

- what application state it represents;
- where Back returns;
- what data it owns;
- what asynchronous operations are active;
- how it behaves while loading;
- what happens on failure.

Avoid global booleans that gradually become an undocumented navigation state machine.

If a screen can be entered from multiple places, model its return destination explicitly instead of hard-coding one parent.

Do not duplicate an entire screen solely because two callers need slightly different return behaviour.

---

## 22. Application feature progression

A reasonable development sequence is:

### Phase 1 — native shell

- boot;
- top/bottom rendering;
- physical input;
- clean exit;
- logging;
- network lifecycle;
- build/install loop.

### Phase 2 — rendering foundation

- real 2D renderer;
- text layout;
- font handling;
- touch hit-testing;
- reusable UI primitives.

### Phase 3 — Wolfram session

- initialise the SDK exactly once;
- establish the 3DS transport/platform relationship;
- authenticate;
- represent loading/error/session states.

### Phase 4 — first Bluesky surface

- home timeline;
- post cards;
- pagination;
- profile viewing;
- thread viewing.

### Phase 5 — interaction

- compose;
- replies;
- likes;
- reposts;
- notifications;
- search.

### Phase 6 — persistence and polish

- settings;
- session persistence;
- caching;
- drafts;
- media;
- accessibility refinement;
- performance work.

Do not skip directly from a text-mode shell to a full-featured social client without establishing the rendering and state-management foundations.

---

## 23. Testing and validation

There are several distinct levels of validation. Do not confuse them.

### Static/code validation

At minimum, compile touched source with the intended warnings enabled where the required toolchain is available.

### Native 3DS build

Run:

```sh
make clean
make
```

and verify the expected `.3dsx` output.

If Wolfram is involved, ensure the actual 3DS Wolfram build used by Indigo is the one being linked.

### Hardware validation

A successful cross-build does not prove that Indigo works on a 3DS.

Hardware acceptance should include, as applicable:

- application launches;
- both screens initialise correctly;
- buttons work;
- touchscreen works;
- application exits cleanly;
- network initialisation succeeds;
- DNS/HTTPS requests work;
- session creation works;
- feed loading works;
- long scrolling does not leak memory;
- losing network connectivity does not hang the UI.

When a feature depends on real hardware behaviour, mark it hardware-tested only after it has actually run on a console.

### Regression validation

When changing a shared Wolfram integration point, build Wolfram's 3DS target as well as Indigo.

Do not claim emulator or host success as hardware success.

---

## 24. Build and dependency pitfalls

Watch for these recurring failure modes:

- missing `DEVKITPRO`;
- missing devkitARM/libctru;
- missing 3DS portlibs;
- Wolfram built for the host instead of 3DS;
- incorrect sibling Wolfram path;
- duplicate socket initialisation;
- accidentally linking desktop libraries;
- object-name collisions caused by flat `VPATH`;
- assuming C++ runtime symbols are linked when the target is built as C;
- using APIs available on desktop but absent from libctru;
- relying on New 3DS-only input on Old 3DS;
- allocating large temporary buffers per frame;
- blocking the main loop on network I/O.

When the linker reports an apparently unrelated symbol failure, check the actual target language, library ordering, object naming and recursive Makefile exports before changing source code.

---

## 25. Code conventions

Keep the existing C style:

- C source unless C++ is justified;
- simple structs;
- explicit ownership;
- clear init/shutdown pairs;
- `bool` for boolean state;
- small functions with one responsibility;
- descriptive module prefixes such as `indigo_app_*`;
- headers that expose only the module's public surface;
- no unnecessary global state.

Keep platform-specific includes in platform-specific modules where possible.

Do not leak libctru types through every application-facing API if a small Indigo abstraction is sufficient.

Comments should explain decisions and hardware constraints, not restate obvious code.

---

## 26. Security rules

Treat AT Protocol credentials and session state as sensitive.

Never commit:

- app passwords;
- access tokens;
- refresh tokens;
- DPoP keys;
- OAuth secrets;
- private test fixtures containing credentials.

Do not add debug endpoints that expose session state.

Do not dump complete authenticated HTTP requests to logs.

When adding cryptographic functionality, prefer Wolfram's existing implementation rather than introducing another crypto library or implementation in Indigo.

---

## 27. Working with the repository

Before editing:

1. Read this file.
2. Read the README.
3. Inspect the relevant module and its callers.
4. Inspect Wolfram's corresponding API if the change crosses the protocol boundary.
5. Check the Makefile if the change affects build inputs or dependencies.
6. Check existing Cobalt behaviour when the requested feature is intentionally shared at the product level.
7. Keep the change scoped.

After editing:

- build what can actually be built;
- run relevant tests;
- inspect the resulting diff;
- update this file when the architecture changes;
- never claim a check was run when it was not.

Use focused commits. Avoid mixing an unrelated cleanup into a feature change.

---

## 28. What not to do

Do not:

- port Cobalt wholesale;
- recreate Wolfram inside Indigo;
- introduce SDL just for API familiarity;
- make the bottom screen a mirrored afterthought;
- make touch the only input path;
- block the render loop on network requests;
- assume permanent network access;
- store credentials in source;
- log session tokens;
- depend on New 3DS-only controls for core navigation;
- make the entire feed resident in memory;
- treat a clean cross-compile as hardware validation;
- add a dependency without checking whether devkitPro actually supplies it;
- silently invent protocol behaviour when Wolfram or the AT Protocol specification already defines it.

---

## 29. Current known state

As of the current scaffold:

- the application has a native `aptMainLoop()`;
- libctru graphics/input initialisation exists;
- top and bottom consoles are separate;
- A/B/START and touchscreen input are translated through `input/`;
- a network module exists;
- an AT Protocol adapter boundary exists;
- Wolfram is not yet called by the AT Protocol adapter;
- the UI is still console-text based;
- Bluesky functionality is not implemented;
- no hardware-tested claim should be made for the scaffold.

The network ownership issue described in §13 must be resolved before the real Wolfram session lifecycle is wired into the application.

---

## 30. Keep this document current

This file is intentionally more detailed than a generic coding-agent guide because Indigo has several platform constraints that are easy to lose when the codebase grows.

When an architectural decision changes — for example:

- choosing citro2d/citro3d or another renderer;
- changing the networking ownership model;
- adding persistent storage;
- adding an offline queue;
- adding OAuth;
- defining a concrete screen/navigation architecture;
- adding CI or host-side tests;
- changing the Wolfram integration boundary;

update the relevant section of this file in the same change.

The code, README and AGENTS.md should describe the same project.
