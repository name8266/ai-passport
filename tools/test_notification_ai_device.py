#!/usr/bin/env python3
"""Run bounded RAM notification tests on an authorized, already flashed device.

Requires CONFIG_HUB_DEVICE_TEST, matching ELF, and OpenOCD with flash probing
disabled. Never flashes, resets, formats archives, or calls the model on the Mac.
Credentials are read from an owner-only JSON file outside the repository.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import stat
import subprocess
import tempfile
import time


def run_gdb(debugger, elf, commands):
    descriptor, name = tempfile.mkstemp(prefix='hub-ai-local-', suffix='.gdb')
    try:
        with os.fdopen(descriptor, 'w', encoding='utf-8') as script:
            script.write('set pagination off\nset confirm off\n')
            script.write('target remote localhost:3333\nmonitor halt\n')
            # Read mapped Flash identity only outside FAT/Flash critical sections.
            script.write('hbreak refresh\ncontinue\ndelete breakpoints\n')
            expected = hashlib.sha256(elf.read_bytes()).hexdigest()
            script.write('python\nimport gdb\n')
            script.write('field=gdb.parse_and_eval("esp_app_desc.app_elf_sha256")\n')
            script.write('actual=bytes(gdb.selected_inferior().read_memory(int(field.address),32)).hex()\n')
            script.write('if actual != '+repr(expected)+': raise RuntimeError("Device ELF mismatch")\nend\n')
            script.write(commands+'\ndetach\nquit\n')
        result = subprocess.run([debugger, '-q', '-batch', str(elf), '-x', name],
                                capture_output=True, text=True, timeout=25)
        if result.returncode:
            # Debugger errors may repeat credential-bearing source lines.
            raise RuntimeError('JTAG command failed; private output withheld')
        return result.stdout
    finally:
        Path(name).unlink(missing_ok=True)
        with socket.create_connection(('localhost', 6666), timeout=3) as channel:
            channel.sendall(b'resume\x1a')
            channel.recv(1024)


def injection(config):
    values = {'hub_test_wifi.ssid': config['ssid'],
              'hub_test_wifi.password': config['password'],
              'hub_test_settings.api_key': config['api_key'],
              'hub_test_settings.endpoint': 'https://api.deepseek.com/chat/completions',
              'hub_test_settings.model': 'deepseek-flash'}
    return '\n'.join([
        'python', 'import gdb, json',
        'values=json.loads('+repr(json.dumps(values))+')',
        'for symbol, value in values.items():',
        '    field=gdb.parse_and_eval(symbol)',
        '    raw=value.encode("utf-8")',
        '    capacity=int(field.type.sizeof)',
        '    if len(raw)>=capacity: raise ValueError("field too long")',
        '    gdb.selected_inferior().write_memory(int(field.address), raw+b"\\0"*(capacity-len(raw)))',
        'end', 'set hub_test_wifi.configured=1',
        'set hub_test_settings.enabled=1',
        'set hub_test_settings.redact_sensitive=1',
        'set hub_test_settings.max_records=3',
        'set hub_test_settings.interval_minutes=60', 'set hub_test_command=1'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gdb', required=True)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--credentials', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if args.credentials.is_symlink():
        parser.error("credentials must not be a symbolic link")
    mode = args.credentials.stat()
    if (not stat.S_ISREG(mode.st_mode) or mode.st_mode & 0o077 or
            mode.st_uid != os.getuid()):
        parser.error('credentials must be an owner-only regular file')
    config = json.loads(args.credentials.read_text())
    if not all(isinstance(config.get(k), str) and config[k]
               for k in ('ssid', 'password', 'api_key')):
        parser.error('credentials require ssid, password, api_key')
    samples = []
    started = time.monotonic()
    try:
        run_gdb(args.gdb, args.elf, injection(config))
        config.clear()
        deadline = time.monotonic()+70
        while time.monotonic()<deadline:
            time.sleep(5)
            output = run_gdb(args.gdb, args.elf,
                'printf "HUB_NETWORK %u %u\\n", ai_online, hub_test_command')
            if 'HUB_NETWORK 1 0' in output:
                break
            if 'HUB_NETWORK 0 4' in output:
                raise RuntimeError('device could not join test Wi-Fi')
        else:
            raise RuntimeError('device network wait timed out')
        run_gdb(args.gdb, args.elf, 'set hub_test_command=2')
        started = time.monotonic()
        while time.monotonic()-started<480:
            time.sleep(5)
            output = run_gdb(args.gdb, args.elf,
                'printf "HUB_RESULT %u %u %u %u %u %u %u\\n", hub_test_command, hub_test_completed, hub_test_passed, hub_test_elapsed_ms, hub_test_min_heap, hub_test_stack, hub_test_low_water')
            for line in output.splitlines():
                if line.startswith('HUB_RESULT '):
                    fields = [int(x) for x in line.split()[1:]]
                    sample = dict(zip(('command', 'completed', 'passed', 'last_ms',
                                       'min_post_request_heap', 'stack_remaining', 'heap_low_water'), fields))
                    sample['observed_seconds'] = round(time.monotonic()-started, 1)
                    samples.append(sample)
                    print(json.dumps(sample), flush=True)
                    if sample['command']==4:
                        return
        raise RuntimeError('bounded test window expired')
    finally:
        config.clear()
        try:
            run_gdb(args.gdb, args.elf, 'if hub_test_command != 4\nset hub_test_command=3\nend')
        except (RuntimeError, OSError, subprocess.TimeoutExpired):
            print('Stop command unavailable; reset clears RAM credentials', flush=True)
        args.report.write_text(json.dumps({'samples': samples,
            'origin': 'ESP32-C3 HTTPS; RAM fixtures; JTAG observations'}, indent=2)+'\n')


if __name__ == '__main__':
    main()
