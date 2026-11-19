#!/usr/bin/env python3
"""Envia los sprites empaquetados a la SD de la placa por USB.

  python3 tools/send_sd.py                  # envia tools/sdcard/mons/*.bin
  python3 tools/send_sd.py --port /dev/cu.usbmodem101
  python3 tools/send_sd.py --ls             # lista lo que hay en la SD
  python3 tools/send_sd.py --only thumbs    # send just the files matching

--only exists because thumbs.bin changes every time the dex grows, and it is
one 415 KB file against ~105 MB of sprites. Without it the only way to fix a
stale gallery was to resend everything.
"""
import argparse
import glob
import os
import struct
import sys
import time
import zlib
import serial


def find_port():
    ports = glob.glob('/dev/cu.usbmodem*') + glob.glob('/dev/ttyACM*')
    if not ports:
        sys.exit("no encuentro la placa (/dev/cu.usbmodem* or /dev/ttyACM*)")
    return ports[0]


def wrap_tpkz(blob, level=9):
    if blob[:4] == b'TPKZ':
        return blob
    comp = zlib.compress(blob, level)
    wrapped = b'TPKZ' + struct.pack('<I', len(blob)) + comp
    return wrapped if len(wrapped) < len(blob) else blob


def read_pak(path):
    data = open(path, 'rb').read()
    if data[:4] != b'TPAK':
        sys.exit(f"{path}: invalid TPAK header")
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
    return items


def wait_line(ser, expect, timeout=10):
    end = time.time() + timeout
    while time.time() < end:
        line = ser.readline().decode(errors='replace').strip()
        if not line:
            continue
        print(f"  placa: {line}")
        if line == expect:
            return True
        if line == 'ERR':
            return False
    return False


def send_blob(ser, name, payload):
    size = len(payload)
    print(f"-> {name} ({size/1024:.1f} KB)")
    ser.write(f"PUT {name} {size}\n".encode())
    if not wait_line(ser, 'OK', 5):
        print("   la placa no acepto el PUT, sigo con el siguiente")
        return False
    t0 = time.time()
    ok = True
    for i in range(0, size, 2048):
        ser.write(payload[i:i + 2048])
        ack = ''
        while ack not in ('#', 'ERR'):
            ack = ser.readline().decode(errors='replace').strip()
            if ack == '':
                ok = False
                break
        if not ok or ack == 'ERR':
            ok = False
            break
    if ok and wait_line(ser, 'DONE', 30):
        kbs = size / 1024 / max(0.01, time.time() - t0)
        print(f"   ok ({kbs:.0f} KB/s)")
        return True
    print("   fallo la transferencia")
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port')
    ap.add_argument('--ls', action='store_true')
    ap.add_argument('--df', action='store_true', help='show LittleFS total/used/free bytes')
    ap.add_argument('--only', help='only send files whose name contains this')
    ap.add_argument('--pak', nargs='+', help='send one or more sprites-*.pak bundles directly')
    ap.add_argument('--raw', action='store_true', help='disable TPKZ compression')
    args = ap.parse_args()

    port = args.port or find_port()
    print(f"puerto {port}")
    ser = serial.Serial(port, 115200, timeout=1)
    time.sleep(1.5)
    ser.reset_input_buffer()

    if args.ls:
        ser.write(b'LS\n')
        wait_line(ser, 'DONE', 10)
        return

    if args.df:
        ser.write(b'DF\n')
        wait_line(ser, 'DONE', 5)
        return

    items = []
    if args.pak:
        for pak_path in args.pak:
            for name, blob in read_pak(pak_path):
                if args.only and args.only not in name:
                    continue
                if not args.raw:
                    blob = wrap_tpkz(blob)
                items.append((name, blob))
    else:
        files = sorted(glob.glob(os.path.join(os.path.dirname(__file__), 'sdcard', 'mons', '*.bin')))
        if not files:
            sys.exit("no hay .bin; ejecuta antes tools/pack_pmd.py o pasa --pak web/sprites-kanto.pak")
        if args.only:
            files = [f for f in files if args.only in os.path.basename(f)]
            if not files:
                sys.exit(f"nothing matches --only {args.only}")
            print(f"--only {args.only}: {len(files)} file(s)")
        for path in files:
            blob = open(path, 'rb').read()
            if not args.raw:
                blob = wrap_tpkz(blob)
            items.append((f"mons/{os.path.basename(path)}", blob))

    ok_cnt = 0
    for name, payload in items:
        if send_blob(ser, name, payload):
            ok_cnt += 1
    print(f"listo ({ok_cnt}/{len(items)})")


if __name__ == '__main__':
    main()
