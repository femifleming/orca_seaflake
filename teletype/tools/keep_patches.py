#!/usr/bin/env python3
"""Make a hex that flashes new firmware but keeps what is on the module.

  tools/keep_patches.py <module-dump.bin> <new-firmware.hex> <out.hex>

<module-dump.bin> is `dfu-programmer at32uc3b0512 read --bin` of the running
module (bootloader mode). The output takes the program from the new firmware
and, from the dump: the ports block (0x80044C00: the Disting EX address set
with ctrl+I, the startup preset), the Seaflake area (0x80044E00: patches,
songs, last scan, last patch) and Teletype's nvram (0x8004E000: scenes,
calibration, the 'already started' flag, so no SCENES WILL BE OVERWRITTEN).

Alt firmware (16 scenes + stock Teletype on alt+o): the new firmware's own
code at 0x80066400 (where scenes 17-32 were) always comes from the new hex;
only scenes 1-16 (0x8004E000-0x80066300) and the settings (0x8007E600) are
kept. Works from a 32-scene dump too (scenes 17-32 are dropped).

Safe only while the firmware keeps those addresses and their layouts
(module/seaflake.lds, module/flash.c); the script checks the addresses in
module/teletype.map against the ones below and refuses otherwise.
"""
import re, sys

BASE = 0x80000000
KEEP_FROM = 0x80044C00   # ports block, Seaflake area, nvram: everything above
DUMP_END = BASE + 0x7E800
SCENES_END = 0x8004E000 + 16 * 6192   # 0x80066300
META = (0x8007E600, 0x8007E600 + 304)  # .flash_meta
CODE_HI = 0x80066400                   # .text_hi, from the new hex

def load_hex(path):
    mem, hi = {}, 0
    for line in open(path):
        line = line.strip()
        if not line.startswith(':'):
            continue
        b = bytes.fromhex(line[1:])
        n, addr, typ = b[0], (b[1] << 8) | b[2], b[3]
        if typ == 4:
            hi = ((b[4] << 8) | b[5]) << 16
        elif typ == 0:
            for i in range(n):
                mem[hi + addr + i] = b[4 + i]
    return mem

def write_hex(mem, path):
    out, hi = [], None
    addrs = sorted(mem)
    i = 0
    while i < len(addrs):
        a = addrs[i]
        if (a >> 16) != hi:
            hi = a >> 16
            rec = bytes([2, 0, 0, 4, (hi >> 8) & 255, hi & 255])
            out.append(':' + rec.hex().upper() + '%02X' % ((-sum(rec)) & 255))
        chunk = [mem[a]]
        while i + len(chunk) < len(addrs) and len(chunk) < 16 and addrs[i + len(chunk)] == a + len(chunk) and ((a + len(chunk)) >> 16) == hi:
            chunk.append(mem[a + len(chunk)])
        rec = bytes([len(chunk), (a >> 8) & 255, a & 255, 0] + chunk)
        out.append(':' + rec.hex().upper() + '%02X' % ((-sum(rec)) & 255))
        i += len(chunk)
    out.append(':00000001FF')
    open(path, 'w').write('\n'.join(out) + '\n')

def check_map():
    try:
        m = open(__file__.rsplit('/tools/', 1)[0] + '/module/teletype.map').read()
    except OSError:
        return
    want = {'.sf_config': 0x80044C00, '.flash_sf': 0x80044E00, '.flash_nvram': 0x8004E000,
            '.text_hi': CODE_HI, '.flash_meta': META[0]}
    for sec, addr in want.items():
        g = re.search(r'^%s\s+0x([0-9a-f]+)' % re.escape(sec), m, re.M)
        if not g or int(g.group(1), 16) != addr:
            sys.exit('layout changed (%s): refusing to keep the old data' % sec)

def main():
    dump, new, out = sys.argv[1:4]
    check_map()
    d = open(dump, 'rb').read()
    if len(d) < DUMP_END - BASE:
        sys.exit('dump too short')
    if d[0x44C00:0x44C04] != b'SFCF':
        sys.exit('the module is not running Orca Seaflake (no ports block): flash normally')
    new_mem = load_hex(new)
    if not any(a >= CODE_HI for a in new_mem):
        sys.exit('the new hex has no code at 0x80066400: not the alt firmware')
    mem = {a: v for a, v in new_mem.items() if a < KEEP_FROM or a >= CODE_HI}
    keep = list(range(KEEP_FROM, SCENES_END)) + list(range(*META))
    for a in keep:
        if a in mem:
            sys.exit('new firmware has code at %08X inside a kept area' % a)
        v = d[a - BASE]
        if v != 0xFF:
            mem[a] = v
    write_hex(mem, out)
    print('kept %d bytes of patches/songs/scenes/settings from the module' % sum(1 for a in keep if a in mem))

main()
