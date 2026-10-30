#!/usr/bin/env python3
"""Extract replay entry points from an unstripped ELF64 shared object."""
import hashlib
import json
from pathlib import Path
import struct
import sys

if len(sys.argv) != 3:
    raise SystemExit('usage: hakux-elf-symbols.py core.so symbols.json')
source, output = map(Path, sys.argv[1:])
data = source.read_bytes()
if data[:6] != b'\x7fELF\x02\x01':
    raise SystemExit('expected little-endian ELF64')
header = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
section_offset, section_size, section_count = header[6], header[11], header[12]
sections = [struct.unpack_from('<IIQQQQIIQQ', data, section_offset+i*section_size)
            for i in range(section_count)]
wanted = {'nc_dev_apu_init', 'nc_dev_apu_stop', 'nc_dev_apu_free',
          'mcpx_apu_dsp_frame', 'g_state', 'dsp_state_finish_load', 'dsp_engine_name'}
found = {}
for section in sections:
    if section[1] != 2:
        continue
    strings = sections[section[6]]
    names = data[strings[4]:strings[4]+strings[5]]
    for offset in range(section[4], section[4]+section[5], section[9]):
        name_offset, info, _, index, value, size = struct.unpack_from('<IBBHQQ', data, offset)
        name = names[name_offset:names.find(b'\0', name_offset)].decode('utf-8', 'replace')
        if name not in wanted or not index or not value:
            continue
        binding = info >> 4
        if name not in found or binding > found[name]['binding']:
            found[name] = {'offset': hex(value), 'size': size, 'binding': binding}
if wanted-found.keys():
    raise SystemExit('missing symbols (unstripped .symtab required): '+str(wanted-found.keys()))
result = {'core': str(source), 'sha256': hashlib.sha256(data).hexdigest(),
          'elf_size': len(data), 'elf_machine': header[2],
          **{name: entry['offset'] for name, entry in sorted(found.items())},
          'symbol_details': found}
output.write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result, indent=2))
