#!/usr/bin/env python3
"""Check the libretro core: its exports, its info, and a synthetic title run through it."""
import argparse
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'linux'))
from check import disc, run  # noqa: E402  (the day-one fixtures)


def dynamic_exports(path):
    """The defined global and weak symbols in an ELF64 object's .dynsym."""
    data = Path(path).read_bytes()
    if data[:4] != b'\x7fELF' or data[4] != 2:
        raise SystemExit(f'FAIL: {path}: not ELF64')
    shoff, = struct.unpack_from('<Q', data, 0x28)
    shentsize, shnum = struct.unpack_from('<HH', data, 0x3a)
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * shentsize) for i in range(shnum)]
    names = []
    for name, kind, _, _, offset, size, link, _, _, entsize in sections:
        if kind != 11:          # SHT_DYNSYM
            continue
        strtab = sections[link]
        for i in range(1, size // entsize):
            st_name, st_info, _, st_shndx = struct.unpack_from('<IBBH', data, offset + i * entsize)
            if st_shndx == 0 or (st_info >> 4) not in (1, 2):   # undefined, or not global/weak
                continue
            start = strtab[4] + st_name
            names.append(data[start:data.index(b'\0', start)].decode())
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('harness', type=Path)
    parser.add_argument('core', type=Path)
    args = parser.parse_args()
    exports = dynamic_exports(args.core)
    stray = [n for n in exports if not n.startswith('retro_')]
    if stray or len(exports) < 25:
        raise SystemExit(f'FAIL: {args.core.name} exports {len(exports)} symbols; not libretro: {stray[:10]}')
    print(f'PASS: {args.core.name} exports only retro_* ({len(exports)})')
    run([args.harness, args.core], 0, ('core: hakuX', 'extensions iso, need_fullpath 1, block_extract 1',
                                       'av: 59.940061 fps, 48000 Hz', 'serialize size: 0', 'unloaded without a game'))
    with tempfile.TemporaryDirectory(prefix='hakux-libretro-') as folder:
        root = Path(folder)
        (root / 'saves').mkdir()
        (root / 'invalid.iso').write_bytes(b'not an Xbox disc')
        run([args.harness, args.core, root / 'missing.iso', 60, root / 'saves'], 3, 'load: refused')
        disc(root / 'halt.iso', b'\xf4')
        run([args.harness, args.core, root / 'halt.iso', 600, root / 'saves'], 0,
            ('guest executed HLT', 'the core asked to shut down', 'the core cannot stop yet'))
        if not (root / 'saves' / 'hakux' / 'hdd' / 'Partition0').is_dir():
            raise SystemExit('FAIL: no console disk under the save directory')
        print('PASS: the console disk is under <save directory>/hakux')


if __name__ == '__main__':
    main()
