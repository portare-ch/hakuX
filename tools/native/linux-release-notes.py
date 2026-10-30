#!/usr/bin/env python3
"""The GitHub release notes of linux-v<version> (.github/workflows/linux.yml):
its CHANGELOG.md section, how to install it and what it was built from.
Fails when CHANGELOG.md has no section for the version.
    python3 tools/native/linux-release-notes.py <version> <commit> [<previous tag> [<run url>]]
"""
from pathlib import Path
import re
import sys

if not 3 <= len(sys.argv) <= 5:
    sys.exit(__doc__)
version, commit = sys.argv[1], sys.argv[2]
previous = sys.argv[3] if len(sys.argv) > 3 else ''
run = sys.argv[4] if len(sys.argv) > 4 else ''
repo = 'https://github.com/portare-ch/hakuX-lr'
tag = f'linux-v{version}'

changelog = (Path(__file__).resolve().parents[2] / 'CHANGELOG.md').read_text()
section = re.search(rf'^## {re.escape(tag)}(?:[ \t][^\n]*)?\n(.*?)(?=^## |\Z)', changelog, re.M | re.S)
if not section:
    sys.exit(f'CHANGELOG.md has no "## {tag}" section')

on_portareos = ('On PortareOS, copy `lib/libretro/hakux_libretro.so` and `.info` to `/storage/cores`. '
                'They take the place of the core PortareOS ships, updates included: delete them to go back. '
                if '-' in version else '')
built = f'Built from `{commit}` using `tools/native/linux-ci.sh {version}` in the pinned image (`tools/native/Dockerfile.linux`)'
built += f' by [CI]({run}).' if run else '.'
print('hakuX for PortareOS on the Retroid Pocket Nova\n')
print(section.group(1).strip() + '\n')
print('**Installing**')
print(f'{on_portareos}Other ARM64 Linux systems can follow the '
      f'[RetroArch installation guide]({repo}/blob/{tag}/README.md#play-on-other-aarch64-linux-systems).\n')
print(built + ' `BUILD-INFO` records the source commit and pinned dependencies.')
if previous:
    print(f'\n[Full changelog]({repo}/compare/{previous}...{tag})')
