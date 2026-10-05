# Updating Indigo

Indigo is a `.3dsx` you launch from the Homebrew Menu, and it can replace that file with a newer release from this repository's GitHub releases. Nothing else is an update source. It never updates without being asked, and it keeps the build you have until the new one has been checked and has started.

What is in place today, and what is not:

| Part | State |
|---|---|
| Release assets the updater reads (`update.json`, `indigo-<version>.3dsx`, its `.sha256`) | made by the `Release` workflow from the tag, and checked against what GitHub actually serves |
| Finishing or undoing an interrupted update at start-up | in the 3DS build; the decisions are tested on the host, including losing power after every single step |
| The screen that checks for an update, shows it and asks you to confirm | not built yet: it waits on Wolfram's shared update module (wolfram#106), so Indigo does not carry its own manifest parser |
| A signature on the release | not yet: it needs a signing key that only the owner can make (see the issue tracker) |

Until that screen exists, update the way you installed it: download `indigo.3dsx` from the [latest release](https://github.com/ewanc26/indigo/releases/latest) and copy it over the old one in `sdmc:/3ds/`.

## How it works

1. Indigo fetches `https://github.com/ewanc26/indigo/releases/latest/download/update.json`. That is GitHub's own redirect to the newest release, so it is one small request and no API rate limit.
2. The manifest is the shape every client in the stack uses (wolfram#106): a version, and one asset with its URL, size and SHA-256. The asset URL must start with `https://github.com/ewanc26/indigo/releases/download/v<that version>/`, so a manifest cannot send the console anywhere else.
3. Only a higher release number is offered. A build made past a tag counts as that tag, so a development build of 0.5.0 is not offered 0.5.0. A build that cannot say which release it is is not offered anything.
4. You see the version and confirm. Nothing is downloaded before that.
5. The build is downloaded to memory with an 8MB ceiling, its size and SHA-256 are checked, it is written to `indigo.3dsx.new` next to the running one, and it is read back from the card and hashed again. A write that succeeded and the right bytes on the card are different things.
6. The swap: the running file becomes `indigo-previous.3dsx`, the new file becomes `indigo.3dsx`, and Indigo exits to the Homebrew Menu. A journal at `sdmc:/3ds/indigo/update.state` records each step first.
7. The next start finishes the job. If the new build starts from `indigo.3dsx`, the previous one is deleted. If the console lost power half way, whichever build you can still launch reads the journal and either completes the swap or puts the old build back; `indigo-previous.3dsx` keeps the `.3dsx` extension precisely so the Homebrew Menu still lists it in that case.

No account token is involved at any point. The release is public, the requests carry no `Authorization` header (Wolfram's `wf_http_get_public`), and the log records versions and steps, never a URL with a query string or anything from the session.

## What I checked, and where

I read the primary sources, I did not take them on trust:

- **Universal-Updater** (Universal-Team/Universal-Updater at commit `4bb0ded`, and its wiki), the usual way 3DS homebrew is installed and updated. Its UniStore format is JSON with per-app scripts of `downloadFile`, `downloadRelease`, `installCia`, `move` and the like, and `%3DSX%` defaults to `sdmc:/3ds`. None of those functions takes a checksum or a signature. Its own self-update downloads to `<argv[0]>.temp`, closes RomFS, deletes the running `.3dsx` and renames the download over it, which leaves a moment with no build on the card at all. That is why Indigo renames rather than deletes and keeps the old file until the new one has started.
- **UniStore, not used.** A UniStore entry would only be a second way to fetch the same file without a checksum, and getting it into Universal-DB is a listing on someone else's index, which is the owner's decision. Nothing here stops anyone writing one, because the release asset names are stable.
- **`.3dsx`, not `.cia`.** Indigo ships only a `.3dsx` (AGENTS.md section 19 covers why: no CIA tooling from devkitPro's packages). A `.3dsx` launched from the Homebrew Menu gets its own path in `argv[0]`, which Universal-Updater relies on as well. A build started any other way (3dslink, a CIA) is not offered self-update.
- **RomFS lives inside the `.3dsx`.** `romfsInit()` reads the file the build was launched from, so the swap closes RomFS first and the app exits straight afterwards, and recovery runs before RomFS is mounted. When the build running is the backup, recovery copies it rather than moving it out from under itself.
- **HTTPS.** Indigo uses Wolfram's transport (curl and mbedTLS with the bundled CA roots), not the system `httpc`/`sslc` services, so there is one TLS stack to reason about. I confirmed with `curl` from my machine that `releases/latest/download/indigo.3dsx` takes two redirects, through `github.com` to `release-assets.githubusercontent.com`, and that the 0.5.0 asset is 1,442,404 bytes. I could not check here which roots those two hosts chain to, because this machine's network intercepts TLS. Whether the bundled roots cover them is one of the things a console run has to show.
- **The SD card.** `rename()` on the card does not replace an existing file, which the settings and session stores already work around, and the swap is written so it never needs it to.

All of the above is from reading and from host tests. The update has not been run on an emulator or on a 3DS.

## Integrity, honestly

A SHA-256 published in the same release as the file proves the download is the file that was uploaded. It does not prove the upload was mine. If the release itself were replaced, the checksum would be replaced with it. A signature over the manifest, checked against a public key built into Indigo, is the fix, and the manifest already has a `signature` field reserved for it. The key has to be the owner's, so it is not something I can generate.
