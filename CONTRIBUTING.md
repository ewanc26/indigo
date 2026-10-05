# Contributing to Indigo

This is the flow for every change, mine or an agent's. It is the same in Wolfram, Cobalt and Platinum where those repositories have adopted it; Indigo's architectural rules are in [AGENTS.md](AGENTS.md).

## The flow

1. Open or find an issue. Anything needing hardware, credentials or money is labelled `needs-owner`.
2. Branch from a fresh `origin/main`. The name is `<type>/<kebab-name>`, for example `fix/status-line-clip`.
3. Commit with `type(scope): subject`. The types are `feat fix docs chore ci refactor test build ui perf release revert`. Keep commits focused.
4. Open a pull request from the template. The title follows the commit format; the body has a `## Summary` and a `## Verification` section, and the verification says exactly what ran and where (host, emulator, hardware). Without devkitARM it says "host only".
5. Update AGENTS.md, README, `docs/` and `CHANGELOG.md` in the same pull request as the change.
6. Wait for the `CI gate` check to be green. A red check is fixed, never skipped, and tests are never disabled to get there.
7. Squash-merge. Nothing is pushed to `main` directly and nothing is force-pushed.

## What CI enforces

`CI gate` waits on three jobs: `Flow checks` (branch name, title, body, commit subjects, doc drift), `Host tests and warnings sweep` and `3DS build`. The flow checks are [scripts/check-flow.sh](scripts/check-flow.sh); [scripts/check-flow-selftest.sh](scripts/check-flow-selftest.sh) feeds each one a deliberate violation on every run and fails if any check lets it through. Run both locally before pushing.

`Require CI gate` as a branch protection rule on `main` is an owner setting I cannot change from here; it is tracked in an issue labelled `needs-owner`. Until it is on, the merge rule is by convention.

## Releasing

Releases are cut only with `scripts/release.sh <version>`, from a clean, up-to-date `main`, with a `## [<version>]` section already in `CHANGELOG.md`. The `Release check` workflow fails a `v*` tag that has no changelog section, is not on `main`, or has no green `CI gate` on its commit.
