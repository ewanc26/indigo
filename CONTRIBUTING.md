# Contributing to Indigo

The flow is the same in all five repositories, and its single written copy is Wolfram's [docs/flow.md](https://github.com/ewanc26/wolfram/blob/main/docs/flow.md). In short: branch from `main`, use Conventional Commits, open a pull request from the template, wait for green CI, and rebase-merge. Nothing goes to `main` directly, nothing is force-pushed, and `main` is never merged into a branch. Indigo's architectural rules are in [AGENTS.md](AGENTS.md).

Issues are written in my voice, in the first person. One an agent wrote ends with `_Written by Claude on my behalf._`. Anything only I can supply (hardware results, credentials, keys) gets the `needs-owner` label.

## What CI enforces

Two checks have to be green:

- `flow / conventions` is Wolfram's reusable workflow, called from `.github/workflows/flow.yml`. It checks the branch name, the PR title and body, commit subjects, and that there are no empty or merge commits. `flow / drift` fails if the PR template or the flow block in AGENTS.md differs from Wolfram's copy.
- `CI gate` waits on the host tests, the 3DS build and `Indigo checks`. `Indigo checks` covers what only this repository needs, in [scripts/check-flow.sh](scripts/check-flow.sh):
  - every path and `make` target the docs name exists;
  - no raw protocol method strings outside Wolfram;
  - the logo and icons match `tools/gen_logo.py`;
  - the update assets verify.

  [scripts/check-flow-selftest.sh](scripts/check-flow-selftest.sh) gives each of those a deliberate violation on every run and fails if any of them lets it through.

Branch protection, meaning requiring those two checks and allowing rebase merges only, is a setting only I can change. It is tracked in #18. Until it is on, the rule is a convention.

## Releasing

Releases are cut only with `scripts/release.sh <version>`, from a clean, up-to-date `main`, with a `## [<version>]` section already in `CHANGELOG.md`. The `Release check` workflow fails a `v*` tag that has no changelog section, is not on `main`, or has no green `CI gate` on its commit. `Release assets` then checks what GitHub serves against `update.json`.
