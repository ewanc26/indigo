# Contributing to Indigo

Indigo is a native Bluesky client for the Nintendo 3DS, designed around the 3DS's own hardware, input model, and two-screen layout. It uses [Wolfram](https://github.com/ewanc26/wolfram) for AT Protocol functionality, supports custom PDS accounts, and is scoped to the Bluesky app rather than general AT Protocol use. Thanks for wanting to help!

Indigo is deliberately **not a port** of [Cobalt](https://github.com/ewanc26/cobalt): the two share a protocol layer and a philosophy, not a UI.

## Getting started

Set up devkitPro with libctru/citro2d per the [README](README.md). CI runs host-side checks; hardware behaviour is verified by the maintainer.

## Notes for this project

- **Two screens are the design.** Changes should honour the 3DS's dual-screen layout and its input model, not cram a phone UI into it.
- Memory and CPU are scarce: keep allocations small and avoid dependencies; if something heavy is unavoidable, say so in the PR.
- Protocol work belongs in Wolfram, not here — Indigo is the client surface.
- The logo and Homebrew Menu icon are generated from the same shapes by `tools/gen_logo.py`; visual identity changes should go through that script rather than editing assets by hand.
- If you can't test on hardware, say what you did verify instead.
- Conventional commits; focused PRs; explain verification in the description.
- AI-assisted PRs welcome; stand behind every line and run what you can.

## Licence

AGPL-3.0 (see [LICENSE](LICENSE)). Contributions accepted under the same terms.