#!/usr/bin/env python3
# Copyright (C) 2026 Dual Stream Studio Inc <hello@dualstream.gg>
# SPDX-License-Identifier: GPL-2.0-or-later
# Fails when any tracked text file contains a codepoint outside printable
# ASCII plus tab and newline. Run with file arguments to check only those
# files (the pre-commit hook does), or with none to scan the whole tree.

import subprocess
import sys
import unicodedata
from pathlib import Path

ALLOWED = {0x09, 0x0A, 0x0D}
BINARY_SUFFIXES = {
    ".png", ".jpg", ".jpeg", ".gif", ".ico", ".icns", ".webp",
    ".zip", ".gz", ".7z", ".dll", ".dylib", ".so", ".exe", ".pdb",
    ".ttf", ".otf", ".woff", ".woff2", ".wav", ".mp3", ".mp4",
}


def tracked_files():
    try:
        out = subprocess.run(
            ["git", "ls-files", "--cached", "--others", "--exclude-standard"],
            capture_output=True, text=True, check=True,
        ).stdout
        return [line for line in out.splitlines() if line]
    except (subprocess.CalledProcessError, FileNotFoundError):
        return [str(p) for p in Path(".").rglob("*") if p.is_file() and ".git" not in p.parts]


def check(path):
    if Path(path).suffix.lower() in BINARY_SUFFIXES:
        return 0
    try:
        data = Path(path).read_bytes()
    except OSError:
        return 0
    if b"\x00" in data:
        return 0
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        print(f"{path}: not valid UTF-8")
        return 1

    errors = 0
    for lineno, line in enumerate(text.splitlines(), 1):
        for col, char in enumerate(line, 1):
            cp = ord(char)
            if 32 <= cp <= 126 or cp in ALLOWED:
                continue
            name = unicodedata.name(char, "UNKNOWN")
            print(f"{path}:{lineno}:{col}: U+{cp:04X} {name}")
            errors += 1
    return errors


def main():
    files = sys.argv[1:] or tracked_files()
    total = sum(check(f) for f in files if Path(f).is_file())
    if total:
        print(f"{total} non-ASCII codepoint(s) found")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
