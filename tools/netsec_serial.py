#!/usr/bin/env python3
"""USB command client and validated header-only PCAP receiver (pyserial)."""
import argparse
import getpass
import pathlib
import struct
import time


def decode_pcap(lines):
    data = bytearray()
    expected = None
    complete = False
    for raw in lines:
        line = raw.decode('ascii', 'replace').strip() if isinstance(raw, bytes) else raw.strip()
        if line.startswith('PCAP_BEGIN '):
            expected = int(line.split()[1])
            if not 0 <= expected <= 64:
                raise ValueError('Invalid record count')
            data.clear()
        elif line.startswith('PCAP_DATA ') and expected is not None:
            chunk = bytes.fromhex(line[10:])
            if len(chunk) > 52 or len(data) + len(chunk) > 24 + 64 * 52:
                raise ValueError('Capture exceeds bounds')
            data.extend(chunk)
        elif line == 'PCAP_END' and expected is not None:
            complete = True
            break
    if not complete or len(data) < 24:
        raise ValueError('Incomplete capture')
    magic, major, minor, zone, sigfigs, snap, link = struct.unpack_from('<IHHiIII', data)
    if (magic, major, minor, zone, sigfigs, snap, link) != (0xA1B2C3D4, 2, 4, 0, 0, 36, 105):
        raise ValueError('Unsupported PCAP header')
    off = 24
    count = 0
    while off < len(data):
        if len(data) - off < 16:
            raise ValueError('Truncated record header')
        seconds, micros, saved, original = struct.unpack_from('<IIII', data, off)
        if saved > 36 or saved > original or micros >= 1000000 or off + 16 + saved > len(data):
            raise ValueError('Invalid record')
        off += 16 + saved
        count += 1
    if count != expected:
        raise ValueError('Record count mismatch')
    return bytes(data)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--port', required=True)
    p.add_argument('--command', default='STATUS')
    p.add_argument('--wifi', metavar='SSID', help='Save own network; password is prompted without echo')
    p.add_argument('--output', type=pathlib.Path, help='Export header-only PCAP')
    args = p.parse_args()
    import serial
    command = args.command
    if args.wifi is not None:
        if any(c in args.wifi for c in '|\r\n'):
            p.error('SSID cannot contain the serial protocol delimiter or newline')
        password = getpass.getpass('Own-network password (empty for open): ')
        if any(c in password for c in '\r\n'):
            p.error('Password cannot contain a newline')
        command = 'WIFI ' + args.wifi + '|' + password
    if args.output:
        command = 'PCAP'
    # Configure DTR/RTS before opening: do not reset the board merely to inspect it.
    port = serial.Serial()
    port.port = args.port
    port.baudrate = 115200
    port.timeout = 0.25
    port.dtr = False
    port.rts = False
    with port:
        port.reset_input_buffer()
        port.write((command + '\n').encode('utf-8'))
        lines = []
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            line = port.readline()
            if not line:
                continue
            if args.output:
                lines.append(line)
                if line.strip() == b'PCAP_END':
                    capture = decode_pcap(lines)
                    args.output.write_bytes(capture)
                    print(f'Saved {len(capture)} bytes to {args.output}')
                    return
            elif b'NETSEC' in line or b'HISTORY' in line:
                print(line.decode('utf-8', 'replace').strip())
                if args.wifi is not None or command in ('STATUS', 'CONNECT', 'FORGET') or command.startswith('PAGE '):
                    return
        if args.output:
            raise SystemExit('PCAP export timed out; no output was written')


if __name__ == '__main__':
    main()
