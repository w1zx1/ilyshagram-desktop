#!/usr/bin/env python3
"""Apply this fork's tweaks to desktop-app/patches build_ffmpeg_win.sh.

Runs from the 'patches' prepare stage with the working directory set to the
freshly checked out patches repo, so it operates on build_ffmpeg_win.sh in
the current directory.

A helper file is used because prepare.py's Windows command builder rejects
any '$' in a command line, and these replacements consist mostly of '$'.
"""

import pathlib
import sys

REPLACEMENTS = [
    (
        'FullExecPath=$PWD',
        'FullExecPath=$(cd "$(dirname "$0")"; pwd)/../ffmpeg',
    ),
    (
        'export PKG_CONFIG_PATH="$FullExecPath/../local/lib/pkgconfig:$PKG_CONFIG_PATH"',
        'export PKG_CONFIG_PATH="$FullExecPath/../local/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"',
    ),
]


def main():
    target = pathlib.Path('build_ffmpeg_win.sh')
    data = target.read_bytes()
    updated = False
    for old, new in REPLACEMENTS:
        old_bytes = old.encode('utf-8')
        new_bytes = new.encode('utf-8')
        if old_bytes in data:
            data = data.replace(old_bytes, new_bytes, 1)
            updated = True
        elif new_bytes not in data:
            print('[ERROR] Pattern not found in {}: {}'.format(target, old))
            return 1
    if updated:
        target.write_bytes(data)
    print('{}: {}'.format(target, 'patched' if updated else 'already patched'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
