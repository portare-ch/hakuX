#!/usr/bin/env python3
"""Check the libretro core: its exports, its info, and a synthetic title run through it."""
import argparse
import os
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

# A title that reads the console's language (ExQueryNonVolatileSetting, which
# logs the EEPROM's video flags, AV region and language), then halts.
SETTINGS = (
    b'\x83\xec\x0c'                  # sub esp, 12: type, value, result length
    b'\x8d\x44\x24\x08\x50'          # push &result length
    b'\x6a\x04'                      # push 4
    b'\x8d\x44\x24\x0c\x50'          # push &value
    b'\x8d\x44\x24\x0c\x50'          # push &type
    b'\x6a\x07'                      # push 7: XC_LANGUAGE
    b'\xff\x15\x00\x0f\x01\x00'      # call [thunk 0]: ExQueryNonVolatileSetting
    b'\xf4'                          # hlt
)


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
    # PortareOS's mode (HAKUX_LR_END_PROCESS) for the runs, whatever the build;
    # the parked mode's own runs come after.
    os.environ['HAKUX_LR_END_PROCESS'] = '1'
    exports = dynamic_exports(args.core)
    stray = [n for n in exports if not n.startswith('retro_')]
    if stray or len(exports) < 25:
        raise SystemExit(f'FAIL: {args.core.name} exports {len(exports)} symbols; not libretro: {stray[:10]}')
    print(f'PASS: {args.core.name} exports only retro_* ({len(exports)})')
    run([args.harness, args.core], 0, ('core: hakuX', 'extensions iso, need_fullpath 1, block_extract 1',
                                       'av: 59.940060 fps, 48000 Hz', 'serialize size: 0', 'unloaded without a game',
                                       'option: hakux_render_scale (Render Scale), default 1',
                                       'option: hakux_crop_overscan (Crop Overscan), default enabled',
                                       'option: hakux_screen_format (Screen Format), default normal',
                                       'option: hakux_language (Language), default auto',
                                       'option: hakux_video_region (Video Region), default auto'))
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
        # The core options reach the console before it boots: the defaults,
        # with RetroArch in German (RETRO_LANGUAGE_GERMAN, XC_LANGUAGE 3),
        # then each set.
        disc(root / 'settings.iso', SETTINGS, imports=(24,))
        env = dict(os.environ, HARNESS_LANGUAGE='4')
        run([args.harness, args.core, root / 'settings.iso', 600, root / 'saves'], 0,
            ('options: render scale 1x, overscan cropped, screen normal, language 3 (auto), video region auto',
             'aspect 1.333',
             'ex: video flags 00400000, AV pack 0, AV region 00400100, language 3', 'guest executed HLT'), env)
        env['HARNESS_OPTIONS'] = ('hakux_render_scale=2;hakux_screen_format=widescreen;hakux_language=french;'
                                  'hakux_video_region=ntsc-j')
        run([args.harness, args.core, root / 'settings.iso', 600, root / 'saves'], 0,
            ('options: render scale 2x, overscan cropped, screen widescreen, language 4 (french), video region ntsc-j',
             'aspect 1.778', 'ex: video flags 00410000, AV pack 0, AV region 00400200, language 4',
             'guest executed HLT'), env)
        # A value the core doesn't know is its default.
        env['HARNESS_OPTIONS'] = 'hakux_render_scale=9;hakux_video_region=pal;hakux_crop_overscan=disabled'
        run([args.harness, args.core, root / 'settings.iso', 600, root / 'saves'], 0,
            ('options: render scale 1x, overscan shown, screen normal, language 3 (auto), video region pal',
             'ex: video flags 00400000, AV pack 0, AV region 00800300, language 3'), env)
        # Changed during the game (the menu), they wait for the next start;
        # the crop doesn't.
        disc(root / 'spin.iso', b'\xeb\xfe')
        env = dict(os.environ, HARNESS_OPTIONS_LATER='hakux_render_scale=3;hakux_video_region=pal;'
                   'hakux_crop_overscan=disabled')
        run([args.harness, args.core, root / 'spin.iso', 60, root / 'saves'], 0,
            ('options: render scale 1x', 'message: Render Scale, Video Region: takes effect when the game starts next',
             'ran 60 frames'), env)
        # Outside PortareOS's build the emulation parks when the game closes,
        # the frontend runs on, and a second game is refused: after a title
        # that stopped, and with the CPU in a loop that never enters the
        # kernel. The process then exits as usual.
        env = dict(os.environ, HAKUX_LR_END_PROCESS='0', HARNESS_LOAD_AGAIN='1')
        for name, frames, marker in (('halt.iso', 600, 'guest executed HLT'), ('spin.iso', 60, 'ran 60 frames')):
            run([args.harness, args.core, root / name, frames, root / 'saves'], 0,
                (marker, 'the emulation is parked for good', 'unloaded',
                 'load: hakuX plays one game a session: restart RetroArch for another', 'load again: refused'), env)
        print('PASS: the relaunch booted the XBE its page named')


if __name__ == '__main__':
    main()
