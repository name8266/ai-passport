#!/usr/bin/env python3
"""Bounded USB acceptance for CONFIG_ARCADE_USB_TEST (requires pyserial)."""
import argparse
import re
import time
from pathlib import Path
import serial

STATE = re.compile(r"state page=(\d+) selected=(\d+) busy=(\d+) auto=(\d+) speed=(\d+) sound=(\d+) completed=(\d+) heap=(\d+) min=(\d+) largest=(\d+) lvfree=(\d+) stack=(\d+) audio=(\d+) writes=(\d+) audio_errors=(\d+) lvpeak=(\d+) lvlargest=(\d+)")
FAILURE = re.compile(r"Guru Meditation|panic'ed|assert failed|Stack protection|watchdog got triggered|allocation failed|initialization failed|audio write failed|audio format setup failed|Allocating layer buffer failed|Out of memory|USB test input queue full|Backtrace:", re.I)


def run(port, log_path, auto_seconds, reset=False):
    with serial.Serial(port, 115200, timeout=0.1) as device, log_path.open('w') as log:
        def receive(seconds):
            end = time.monotonic() + seconds
            chunks = []
            tail = ''
            received = 0
            while time.monotonic() < end:
                data = device.read(device.in_waiting or 1).decode('utf-8', errors='replace')
                log.write(data)
                log.flush()
                received += len(data)
                assert received < 8 * 1024 * 1024, 'Excessive device log output'
                if FAILURE.search(tail + data):
                    raise AssertionError('Device reported a runtime failure; inspect local log')
                tail = (tail + data)[-512:]
                chunks.append(data)
            return ''.join(chunks)

        def key(command):
            device.write(command.encode('ascii'))
            receive(0.15)

        def state():
            device.write(b'?')
            output = receive(0.5)
            matches = STATE.findall(output)
            assert matches, 'USB state response missing'
            values = [int(v) for v in matches[-1]]
            return dict(zip(('page', 'selected', 'busy', 'auto', 'speed', 'sound',
                             'completed', 'heap', 'min', 'largest', 'lvfree', 'stack',
                             'audio', 'writes', 'audio_errors', 'lvpeak', 'lvlargest'), values))

        def wait_idle(limit=10):
            deadline = time.monotonic() + limit
            while time.monotonic() < deadline:
                current = state()
                if not current['busy']:
                    return current
            raise AssertionError('Game did not finish within timeout')

        if reset:
            from esptool.reset import HardReset
            device.setDTR(False)
            device.setRTS(False)
            time.sleep(0.1)
            HardReset(device, uses_usb=True)()
        startup = receive(3)
        if reset:
            assert 'Odds Arcade ready:' in startup, 'Application startup not confirmed'
        key('O')
        baseline = state()
        assert baseline['page'] == 0
        for index in range(4):
            key('O')
            current = state()
            for _ in range(4):
                if current['selected'] == index:
                    break
                key('d')
                current = state()
            assert current['selected'] == index, 'Home selection did not advance'
            key('o')
            assert state()['page'] == index + 1
            for speed in range(3):
                current = state()
                for _ in range(3):
                    if current['speed'] == speed:
                        break
                    key('u')
                    current = state()
                assert current['speed'] == speed, 'Speed selection did not advance'
                before = current['completed']
                key('o')
                final = wait_idle()
                assert final['completed'] == before + 1
            key('d')
            assert state()['sound'] == 0
            key('d')
            assert state()['sound'] == 1
            before = state()['completed']
            key('U')
            assert state()['auto'] == 1
            receive(auto_seconds)
            key('U')
            final = wait_idle()
            assert final['auto'] == 0 and final['completed'] > before
            # Exit while an animation still owns the page's view objects.
            key('o')
            key('O')
            assert state()['page'] == 0
            print(f'Page {index + 1}: 3 speeds, audio toggles, autoplay and busy exit PASS', flush=True)
        # Repeated deletion/recreation exposes stale pointers and heap leaks.
        for _ in range(40):
            key('d')
            key('o')
            key('o')
            key('O')
        receive(2)
        final = state()
        assert final['page'] == 0 and not final['busy']
        assert final['heap'] >= baseline['heap'] - 2048, (baseline, final)
        assert final['lvfree'] >= baseline['lvfree'] - 1024, (baseline, final)
        assert final['audio'] == 1 and final['writes'] > 0 and final['audio_errors'] == 0, final
        assert final['lvpeak'] < 92 * 1024, final
        assert final['min'] > 8192 and final['stack'] > 512, final
        complete_log = log_path.read_text()
        assert not FAILURE.search(complete_log), 'Runtime failure found in complete log'
        if reset:
            assert complete_log.count('Odds Arcade ready:') == 1, 'Unexpected device restart'
        print(f'Device acceptance PASS: completed={final["completed"]}, heap={final["heap"]}, '
              f'min_heap={final["min"]}, lvfree={final["lvfree"]}, USB_stack={final["stack"]}, '
              f'PCM_writes={final["writes"]}, audio_errors={final["audio_errors"]}, LV_peak={final["lvpeak"]}', flush=True)
        print(f'Local log: {log_path}', flush=True)
        return final


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--reset', action='store_true', help='Reset the authorized target and verify startup')
    parser.add_argument('--auto-seconds', type=float, default=12)
    args = parser.parse_args()
    assert 5 <= args.auto_seconds <= 60
    run(args.port, args.log, args.auto_seconds, args.reset)
