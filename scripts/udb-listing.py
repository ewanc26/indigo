#!/usr/bin/env python3
"""Build Indigo's Universal-DB listing from the repository (docs/UNIVERSAL-DB.md).

    scripts/udb-listing.py            write meta/universal-db/indigo.json
    scripts/udb-listing.py --check    fail if the committed file is stale or
                                      the listing no longer fits the release

The entry follows Universal-DB's source/apps/*.json format (Universal-Team/db,
CONTRIBUTING.md and docs/assets/js/app-request.js). Universal-DB fetches the
title, author, version, release notes and downloads from the GitHub API on its
own schedule, so the entry holds only what it cannot fetch: systems,
categories, icon, banner, the LLM declaration, the long description and the
download filter that picks indigo.3dsx out of the release assets.

The screenshots are not committed; scripts/udb-submit.sh renders them with the
host snapshot renderer at submission time. Standard library only.
"""
import json
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "meta", "universal-db", "indigo.json")
REPO = "ewanc26/indigo"
RAW = f"https://raw.githubusercontent.com/{REPO}/main"
# The one asset Universal-Updater should install. update.json, the versioned
# copy and its checksum are for Indigo's own updater and must not be offered.
DOWNLOAD_FILTER = r"^indigo\.3dsx$"
# Universal-DB generates screenshots' captions from their file names.
SCREENSHOTS = [
    ("timeline-both.png", "home-timeline.png"),
    ("thread-both.png", "a-thread.png"),
    ("profile-both.png", "a-profile.png"),
    ("compose-reply-both.png", "writing-a-reply.png"),
    ("notifications-both.png", "notifications.png"),
    ("settings-both.png", "settings.png"),
]


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(24)
    if head[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} is not a PNG")
    return struct.unpack(">II", head[16:24])


def readme_description():
    """The README's one-sentence description, and the intro after it."""
    text = open(os.path.join(ROOT, "README.md"), encoding="utf8").read()
    m = re.search(r"^# Indigo\n\n(.+?)\n\n(.*?)\n\n## ", text, re.S | re.M)
    if not m:
        raise ValueError("README.md has no '# Indigo' heading followed by a description")
    short = m.group(1).strip()
    intro = m.group(2)
    # Keep prose paragraphs only: drop the version line, the art credit and images.
    paras = [p.strip() for p in intro.split("\n\n")]
    paras = [p for p in paras if p and not p.startswith("**Version") and "gen_logo" not in p
             and not p.startswith("<")]
    long = "\n\n".join(" ".join(line.strip() for line in p.splitlines()) for p in paras)
    return short, long


def entry():
    short, long = readme_description()
    long += ("\n\nIndigo installs to `sdmc:/3ds/indigo.3dsx` and runs from the Homebrew Menu. "
             "It needs a Bluesky account; signing in takes an app password, or a hosted "
             "sign-in node for browser sign-in. Status, and what has and has not been run on "
             f"a real 3DS: https://github.com/{REPO}#status")
    return {
        "github": REPO,
        "title": "Indigo",
        "description": short,
        "systems": ["3DS"],
        "categories": ["app"],
        "icon": f"{RAW}/assets/icon.png",
        "image": f"{RAW}/assets/banner.png",
        # The repository has commits co-authored by Claude, so under
        # Universal-DB's rules this is "yes", never "minor".
        "llm_generation": "yes",
        "download_filter": DOWNLOAD_FILTER,
        "long_description": long,
    }


def render():
    return json.dumps(entry(), indent="\t", ensure_ascii=False) + "\n"


def problems():
    out = []
    e = entry()
    if len(e["description"]) > 256:
        out.append("description is longer than Universal-DB's 256 characters")
    for rel, want in (("assets/icon.png", (48, 48)), ("assets/banner.png", (256, 128))):
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            out.append(f"{rel} is missing")
        elif png_size(path) != want:
            out.append(f"{rel} is {png_size(path)}, Universal-DB wants {want}")
    # The filter has to match the asset the Release workflow publishes, and
    # nothing else it publishes.
    wf = open(os.path.join(ROOT, ".github", "workflows", "release.yml"), encoding="utf8").read()
    published = re.findall(r'release/([A-Za-z][^\s"\\]*)', wf)
    names = {n.replace("$v", "9.9.9") for n in published}
    if "indigo.3dsx" not in names:
        out.append("release.yml no longer publishes indigo.3dsx, which Universal-DB lists")
    for n in names:
        if (re.search(DOWNLOAD_FILTER, n) is not None) != (n == "indigo.3dsx"):
            out.append(f"download_filter would offer {n}")
    snap = os.path.join(ROOT, "tools", "snapshot.c")
    snap_src = open(snap, encoding="utf8").read()
    for src, _ in SCREENSHOTS:
        scenario = src[: -len("-both.png")]
        if f'"{scenario}"' not in snap_src:
            out.append(f"tools/snapshot.c has no '{scenario}' scenario for screenshot {src}")
    return out


def main():
    check = "--check" in sys.argv[1:]
    bad = problems()
    data = render()
    if check:
        try:
            same = open(OUT, encoding="utf8").read() == data
        except FileNotFoundError:
            same = False
        if not same:
            bad.append("meta/universal-db/indigo.json is stale; run scripts/udb-listing.py")
    else:
        os.makedirs(os.path.dirname(OUT), exist_ok=True)
        with open(OUT, "w", encoding="utf8") as f:
            f.write(data)
    for b in bad:
        print("udb-listing:", b, file=sys.stderr)
    if "--screenshots" in sys.argv[1:]:
        for src, dst in SCREENSHOTS:
            print(src, dst)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
