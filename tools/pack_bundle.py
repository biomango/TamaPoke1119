#!/usr/bin/env python3
"""Empaqueta los sprites de la SD (tools/sdcard/mons/*.bin) en un .pak POR
REGION para que el instalador web los suba de un clic.

ONE FILE PER REGION, not one big one, and that is forced rather than chosen:
Kanto alone is already ~40 MB, so all three would be ~100 MB -- exactly GitHub's
hard per-file limit, which would make the bundle uncommittable. Splitting also
means a player can install Kanto and stop, which is the whole 40 MB most people
want, and add a region later without re-sending what they already have.

Formato TPAK (little-endian):
  char[4]  "TPAK"
  uint16   count
  count x { uint8 nameLen; char name[nameLen]; uint32 size }   (indice)
  ...datos de cada fichero, en el mismo orden...

El instalador (web/index.html) lo descarga, lo parte por el indice y manda cada
fichero a la placa con el protocolo PUT (igual que tools/send_sd.py).
"""
import argparse
import glob
import os
import struct
import zlib

HERE = os.path.dirname(__file__)
MONS = os.path.join(HERE, 'sdcard', 'mons')
WEB = os.path.join(HERE, '..', 'web')

# DERIVED from dex_data.py, never a second copy of it. The old line here said
# "Must match REGIONS in dex_data.py" and then silently did not: Sinnoh landed
# in the dex and this still stopped at Hoenn, so no sinnoh pack was ever built
# and the installer could not offer one. Same trap as gen_moves.py keeping its
# own DEX_COUNT = 151 while dex.h had grown.
import sys as _sys
_sys.path.insert(0, HERE)
from dex_data import REGIONS as _DEX_REGIONS
REGIONS = [(name.lower(), lo, hi) for name, lo, hi, _starters in _DEX_REGIONS]

GITHUB_LIMIT = 100 * 1024 * 1024


def dex_of(path):
    base = os.path.basename(path)
    digits = ''.join(c for c in base if c.isdigit())
    return int(digits) if digits else 0


def wrap_tpkz(blob, level=9):
    """Wrap raw TPK2/TPTH/TPK1 bytes in a TPKZ (zlib) container if smaller."""
    if blob[:4] == b'TPKZ':
        return blob
    comp = zlib.compress(blob, level)
    wrapped = b'TPKZ' + struct.pack('<I', len(blob)) + comp
    return wrapped if len(wrapped) < len(blob) else blob


def read_pak(path):
    data = open(path, 'rb').read()
    if data[:4] != b'TPAK':
        raise ValueError(f'{path}: invalid TPAK header')
    count, = struct.unpack_from('<H', data, 4)
    off = 6
    entries = []
    for _ in range(count):
        nl = data[off]
        off += 1
        name = data[off:off + nl].decode()
        off += nl
        sz, = struct.unpack_from('<I', data, off)
        off += 4
        entries.append((name, sz))
    items = []
    for name, sz in entries:
        items.append((name, data[off:off + sz]))
        off += sz
    if off != len(data):
        raise ValueError(f'{path}: trailing bytes ({len(data) - off})')
    return items


def write_pak_items(out, items):
    with open(out, 'wb') as o:
        o.write(b'TPAK')
        o.write(struct.pack('<H', len(items)))
        for name, blob in items:
            nb = name.encode()
            o.write(struct.pack('<B', len(nb)))
            o.write(nb)
            o.write(struct.pack('<I', len(blob)))
        for _, blob in items:
            o.write(blob)
    return sum(len(b) for _, b in items)


def write_pak(out, files, compress=True):
    items = []
    for f in files:
        blob = open(f, 'rb').read()
        if compress:
            blob = wrap_tpkz(blob)
        items.append(('mons/' + os.path.basename(f), blob))
    return write_pak_items(out, items)


def repack_web(compress=True):
    paks = sorted(glob.glob(os.path.join(WEB, 'sprites-*.pak')))
    if not paks:
        raise SystemExit('no sprites-*.pak found in ' + WEB)
    for pak in paks:
        old_size = os.path.getsize(pak)
        items = read_pak(pak)
        new_items = []
        for name, blob in items:
            if compress:
                blob = wrap_tpkz(blob)
            elif blob[:4] == b'TPKZ':
                raw_len, = struct.unpack_from('<I', blob, 4)
                blob = zlib.decompress(blob[8:])
                assert len(blob) == raw_len
            new_items.append((name, blob))
        write_pak_items(pak, new_items)
        new_size = os.path.getsize(pak)
        print(f'{os.path.normpath(pak)}: {len(new_items)} files, '
              f'{old_size / 1048576:.2f} MB -> {new_size / 1048576:.2f} MB')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--raw', action='store_true',
                    help='pack uncompressed sprites instead of TPKZ')
    ap.add_argument('--repack-web', action='store_true',
                    help='recompress existing web/sprites-*.pak files in-place')
    args = ap.parse_args()
    compress = not args.raw

    if args.repack_web:
        repack_web(compress=compress)
        return

    files = sorted(glob.glob(os.path.join(MONS, '*.bin')))
    if not files:
        raise SystemExit('no hay sprites en ' + MONS)
    # Files with no dex number in the name -- thumbs.bin -- are SHARED, not part
    # of any region. Splitting by dex range alone dropped thumbs.bin on the
    # floor and the board says so at boot: "sin thumbs.bin (galeria sin
    # miniaturas)".
    #
    # They go in EVERY pack, not just the first. Riding only with Kanto meant
    # anyone who installed Johto or Hoenn WITHOUT Kanto -- which nothing stops
    # them doing -- got no thumbnails at all: bare dex numbers in place of every
    # sprite in the gallery, and no silhouettes. It costs 415 KB against a 27-40
    # MB pack, so there is nothing to weigh up.
    shared = [f for f in files if dex_of(f) == 0]
    made = 0
    for name, lo, hi in REGIONS:
        mine = [f for f in files if lo <= dex_of(f) <= hi]
        if mine:
            mine = sorted(mine + shared)
        if not mine:
            print(f'{name}: no sprites packed yet, skipped')
            continue
        out = os.path.join(WEB, f'sprites-{name}.pak')
        total = write_pak(out, mine, compress=compress)
        size = os.path.getsize(out)
        flag = '  !! OVER GITHUB LIMIT' if size > GITHUB_LIMIT else ''
        mode = 'TPKZ' if compress else 'raw'
        print(f'{os.path.normpath(out)} [{mode}]: {len(mine)} sprites, '
              f'{total / 1048576:.2f} MB datos ({size / 1048576:.2f} MB total){flag}')
        made += 1
    if not made:
        raise SystemExit('nothing packed')
    print(f'{len(shared)} shared file(s) went into EVERY pack, so any single region works alone')


if __name__ == '__main__':
    main()
