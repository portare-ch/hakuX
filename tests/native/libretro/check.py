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

# A title that relaunches itself once (XLaunchNewImage): with no launch data
# page, it leaves one naming its own XBE and quick-reboots; booted again with
# the page, it halts.
RELAUNCH = (
    b'\xa1\x04\x0f\x01\x00'          # mov eax, [thunk 1]: &LaunchDataPage
    b'\x83\x38\x00'                  # cmp dword [eax], 0
    b'\x75\x10'                      # jne halt
    b'\xc7\x00\x00\x20\x01\x00'      # mov dword [eax], 0x12000: the page below
    b'\x6a\x02'                      # push 2: ReturnFirmwareQuickReboot
    b'\xff\x15\x00\x0f\x01\x00'      # call [thunk 0]: HalReturnToFirmware
    b'\xeb\xfe'                      # jmp $
    b'\xf4'                          # halt: hlt
).ljust(0x1000, b'\0') + struct.pack('<II', 1, 0) + b'\\Device\\CdRom0;default.xbe'


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
                                       'av: 59.940060 fps, 48000 Hz', 'serialize size: 0', 'unloaded without a game'))
    with tempfile.TemporaryDirectory(prefix='hakux-libretro-') as folder:
        root = Path(folder)
        (root / 'saves').mkdir()
        (root / 'invalid.iso').write_bytes(b'not an Xbox disc')
        run([args.harness, args.core, root / 'missing.iso', 60, root / 'saves'], 3, 'load: refused')
        disc(root / 'halt.iso', b'\xf4')
        run([args.harness, args.core, root / 'halt.iso', 600, root / 'saves'], 0,
            ('guest executed HLT', 'the core asked to shut down', 'the core cannot stop yet'))
        if not (root / 'saves' / 'hdd' / 'Partition0').is_dir():
            raise SystemExit('FAIL: no console disk in the save directory')
        print('PASS: the console disk is in the save directory')
        # The frontend (here the harness) starts again in its place and boots
        # the XBE the launch data page names.
        disc(root / 'relaunch.iso', RELAUNCH, imports=(49, 164))
        run([args.harness, args.core, root / 'relaunch.iso', 600, root / 'saves'], 0,
            ('relaunch into \\default.xbe', 'relaunch: starting RetroArch again',
             'relaunch: \\Device\\CdRom0\\default.xbe', 'guest executed HLT', 'the core cannot stop yet'))
        if (root / 'saves' / 'relaunch.bin').exists():
            raise SystemExit('FAIL: the relaunch left its launch data page')
        print('PASS: the relaunch booted the XBE its page named')


if __name__ == '__main__':
    main()
