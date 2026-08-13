#!/usr/bin/env python3
# Copyright (C) 2026 Dual Stream Studio Inc <hello@dualstream.gg>
# SPDX-License-Identifier: GPL-2.0-or-later
# Fails when any tracked source file exceeds the line ceiling. Run with file
# arguments to check only those files (the pre-commit hook does), or with none
# to scan the whole tree.

import subprocess
import sys
from pathlib import Path

LIMIT = 500

SOURCE_SUFFIXES = {".c", ".h", ".cpp", ".hpp", ".m", ".mm", ".py", ".sh"}

# Upstream template files are carried verbatim so they stay diffable against
# obs-plugintemplate; splitting them would make every future rebase a merge.
EXEMPT_PREFIXES = ("build-aux/", ".github/", "cmake/")


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
    posix = Path(path).as_posix()
    if Path(path).suffix.lower() not in SOURCE_SUFFIXES:
        return 0
    if posix.startswith(EXEMPT_PREFIXES):
        return 0
    try:
        count = len(Path(path).read_bytes().splitlines())
    except OSError:
        return 0
    if count <= LIMIT:
        return 0
    print(f"{path}: {count} lines, limit {LIMIT}, split it")
    return 1


def main():
    files = sys.argv[1:] or tracked_files()
    total = sum(check(f) for f in files if Path(f).is_file())
    if total:
        print(f"{total} file(s) over {LIMIT} lines")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
