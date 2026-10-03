#!/usr/bin/env python3
"""Build romfs/cacert.pem: a curated subset of the Mozilla root store.

The 3DS has no usable system trust store and RAM is small, so Indigo ships only
the roots that public Bluesky services and common custom PDS hosts chain to.
Usage: tools/make_cabundle.py <mozilla cacert.pem> [output]
Each certificate's SHA-256 fingerprint is written above it so a change to the
bundle shows up in review.
"""
import re
import subprocess
import sys

WANTED = [
    "ISRG Root X1",
    "ISRG Root X2",
    "DigiCert Global Root G2",
    "DigiCert Global Root CA",
    "DigiCert Global Root G3",
    "GTS Root R1",
    "GTS Root R2",
    "GTS Root R3",
    "GTS Root R4",
    "Amazon Root CA 1",
    "Amazon Root CA 2",
    "Amazon Root CA 3",
    "Amazon Root CA 4",
    "USERTrust RSA Certification Authority",
    "USERTrust ECC Certification Authority",
    "GlobalSign Root E46",
    "GlobalSign Root R46",
]

CERT = re.compile(r"-----BEGIN CERTIFICATE-----\n.*?-----END CERTIFICATE-----\n", re.S)


def describe(pem):
    """Return (common name, SHA-256 fingerprint) using the openssl CLI."""
    out = subprocess.run(
        ["openssl", "x509", "-noout", "-subject", "-fingerprint", "-sha256",
         "-nameopt", "multiline"],
        input=pem, capture_output=True, text=True, check=True).stdout
    cn = re.search(r"commonName\s*=\s*(.+)", out)
    fp = re.search(r"Fingerprint=([0-9A-F:]+)", out)
    return (cn.group(1).strip() if cn else ""), fp.group(1).replace(":", "").lower()


def main():
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else "romfs/cacert.pem"
    text = open(src, encoding="utf-8").read()
    found = {}
    for m in CERT.finditer(text):
        cn, fp = describe(m.group(0))
        found[cn] = (fp, m.group(0))
    missing = [n for n in WANTED if n not in found]
    chunks = [f"# {n}\n# SHA-256 {found[n][0]}\n{found[n][1]}" for n in WANTED if n in found]
    with open(out, "w", encoding="utf-8") as f:
        f.write("".join(chunks))
    print(f"wrote {len(chunks)} roots to {out}")
    if missing:
        print("not in source bundle:", ", ".join(missing), file=sys.stderr)


main()
