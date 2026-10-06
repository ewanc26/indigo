# Universal-DB

[Universal-DB](https://db.universal-team.net) is the index of 3DS and DS homebrew that [Universal-Updater](https://github.com/Universal-Team/Universal-Updater) installs from, so a listing there is how most people with a modded 3DS would find and install Indigo. This page is what I found out about getting listed and what this repository does about it. Indigo is not listed yet.

## How a listing works

I read the source rather than a summary: Universal-Team/db at commit `b16ccb6` (5 October 2026), meaning its README, its `CONTRIBUTING.md`, its app-request issue template and web form script, and its `source/generate.py`.

- An app is one JSON file in `source/apps/`. For a GitHub project it needs `github`, `systems`, `categories` and `icon`. An `image` (a 256x128 banner) and `llm_generation` are expected; the rest is optional.
- Universal-DB fetches everything else from the GitHub API on its own: title, author, latest version, release notes, licence and the release's downloads. It refreshes every six hours, or hourly for apps updated in the last month. **After the first listing, a new GitHub release needs nothing from me.**
- Every release asset becomes a download unless a blacklist catches it, or `download_filter` says otherwise. A `.3dsx` download is installed to `%3DSX%/<name>`, which is `sdmc:/3ds/` by default. Indigo's releases also carry `update.json`, `indigo-<version>.3dsx` and its `.sha256` for Indigo's own updater, so the listing sets `download_filter` to `^indigo\.3dsx$`. Universal-Updater then offers exactly one file, and it lands at `sdmc:/3ds/indigo.3dsx`.
- Screenshots live in the db repository itself, under `docs/assets/images/screenshots/<title>/`. Universal-DB makes each caption from the file name.
- Submitting is a pull request adding those files, or an "App request" issue with the JSON attached. Their template says pull requests are reviewed faster.

## Their rules that matter here

From `CONTRIBUTING.md`:

- **LLM-generated content must be declared.** Indigo's history has commits co-authored by Claude, and their definition puts that outside "minor", so the listing says `"llm_generation": "yes"`. Misdeclaring it is grounds for removal.
- **LLM tools may not write the pull request or issue description, or code and assets for Universal-DB itself.** So the submission script leaves the description empty for me to write. The JSON entry is generated from this repository's own files by a script (below), and I should look over it before submitting. If the maintainers consider a generated entry to be "an asset for Universal-DB itself", the clean fallback is their own form at <https://db.universal-team.net/app-request>, which builds the same JSON from the GitHub repository.
- No piracy, no NSFW content, a description of what the app does, and real functionality. Indigo has no trouble with any of these.

## What this repository does

- `scripts/udb-listing.py` writes `meta/universal-db/indigo.json` from the README's description and intro, the generated icon and banner (`tools/gen_logo.py`), and the asset name the Release workflow publishes. CI runs it with `--check`: it fails if the committed entry is stale, if the icon or banner is the wrong size, or if `download_filter` would offer any release asset other than `indigo.3dsx`.
- `scripts/udb-submit.sh` is the one step for me. It checks the entry, renders the screenshots with the host snapshot renderer, forks Universal-Team/db under my account, commits `source/apps/indigo.json` and the screenshots on a branch, pushes it, and opens GitHub's pull request page with the title filled in. I write the description and press the button. CI runs it with `--dry-run`, which builds the same files without touching GitHub.

The screenshots come from the host snapshot renderer with made-up accounts, not from a console, because the real emulator captures in `docs/screenshots` show other people's posts. Their font is the renderer's, not the 3DS system font. If I want console captures instead, I can replace the files on the PR branch before submitting.

## How it fits Indigo's own updater

Both use the same file from the same release. Universal-Updater downloads `indigo.3dsx` to `sdmc:/3ds/indigo.3dsx`; Indigo's updater (docs/UPDATE.md) replaces whichever `.3dsx` it was launched from, wherever that is. A copy installed by either one can be updated by the other. Neither needs anything the release does not already carry, and the asset name `indigo.3dsx` must never change, because both depend on it.
