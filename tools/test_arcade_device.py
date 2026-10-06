#!/usr/bin/env python3
"""Bounded acceptance via the same input queue as physical buttons."""
import argparse
import re
import time
from pathlib import Path
import serial

FAILURE = re.compile(r"Guru Meditation|panic'ed|assert failed|Stack protection|watchdog got triggered|allocation failed|initialization failed|audio write failed|audio format setup failed|Allocating layer buffer failed|Out of memory|USB test input queue full|Backtrace:", re.I)


def run(port, log_path, rounds, reset=False, games=(0, 1, 2, 3)):
    with serial.Serial(port, 115200, timeout=0.1) as device, log_path.open('w') as log:
        def receive(seconds):
            end = time.monotonic() + seconds
            chunks = []
            while time.monotonic() < end:
                data = device.read(device.in_waiting or 1).decode('utf-8', errors='replace')
                log.write(data)
                log.flush()
                chunks.append(data)
            output = ''.join(chunks)
            assert not FAILURE.search(output), 'Runtime failure; inspect log'
            return output

        def key(command):
            device.write(command.encode('ascii'))
            return receive(0.18)

        def state():
            device.write(b'?')
            output = receive(0.35)
            lines = re.findall(r'state page=.*', output)
            assert lines, 'Missing state response'
            return {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', lines[-1])}

        def audit():
            output = key('v') + receive(0.3)
            match = re.search(r'UI_AUDIT page=(\d+) labels=(\d+) clipped=(\d+) overlaps=(\d+) cube_mesh=(\d+)', output)
            assert match, 'Missing UI audit'
            assert tuple(map(int, match.groups()[2:])) == (0, 0, 1), output

        def idle(limit=12):
            deadline = time.monotonic() + limit
            while time.monotonic() < deadline:
                current = state()
                if not current['busy']:
                    return current
            raise AssertionError('Game completion timeout')

        def home():
            if state()['page'] != 0:
                key('O')
            assert state()['page'] == 0

        def enter(index):
            home()
            for _ in range(4):
                if state()['selected'] == index:
                    break
                key('d')
            key('o')
            assert state()['page'] == index + 1

        if reset:
            from esptool.reset import HardReset
            device.setDTR(False)
            device.setRTS(False)
            HardReset(device, uses_usb=True)()
        startup = receive(4)
        if reset:
            assert 'Odds Arcade ready:' in startup, startup
            assert re.search(r'FONT_AUDIT .*missing=0 negative_ok=1', startup), startup
        home()
        baseline = state()
        assert baseline['backlight'] == 100 and baseline['fps_overlay'] == 0
        audit()
        key('O')
        assert state()['page'] == 5
        audit()
        key('o')
        assert state()['fps_overlay'] == 1
        audit()
        key('o')
        assert state()['fps_overlay'] == 0
        key('d')  # Dice count row: cycle all six dice counts.
        counts = set()
        for _ in range(6):
            key('o')
            counts.add(state()['dice_count'])
            audit()
        assert counts == set(range(1, 7))
        key('d')
        modes = set()
        for _ in range(3):
            key('o')
            modes.add(state()['neon'])
            audit()
        assert modes == {0, 1, 2}
        key('d')
        key('o')
        assert state()['sound'] == 0
        key('o')
        assert state()['sound'] == 1
        key('d')
        while state()['speed'] != 2:
            key('o')
        key('d')  # New sixth row selects the slot mode.
        key('o')
        assert state()['slot_mode'] == 1
        audit()
        key('o')
        assert state()['slot_mode'] == 0
        key('O')
        cases = [(index, mode) for index in games
                 for mode in ((0, 1) if index == 0 else (None,))]
        for index, mode in cases:
            if index == 0 and state()['slot_mode'] != mode:
                home()
                key('O')
                assert state()['page'] == 5 and state()['settings_row'] == 5
                key('o')
                assert state()['slot_mode'] == mode
                audit()
                key('O')

            enter(index)
            audit()
            if index == 2:
                for count in range(1, 7):
                    while state()['dice_count'] != count:
                        key('u')
                    before = state()['completed']
                    key('o')
                    assert idle()['completed'] == before + 1
                    audit()
                assert state()['dice_count'] == 6
            elif index == 3:
                for count in range(1, 11):
                    while state()['plinko_count'] != count:
                        key('u')
                    before = state()['completed']
                    key('o')
                    assert idle()['completed'] == before + 1
                    audit()
            else:
                for speed in range(3):
                    while state()['speed'] != speed:
                        key('u')
                    before = state()['completed']
                    key('o')
                    assert idle()['completed'] == before + 1
                    audit()
                assert state()['speed'] == 2
            key('d')
            assert state()['sound'] == 0
            key('d')
            assert state()['sound'] == 1
            before = state()['completed']
            target = before + rounds
            key('U')
            assert state()['auto'] == 1
            key('m')  # USB observer sleeps for its bounded sampling window.
            receive(11)
            deadline = time.monotonic() + rounds * 10 + 30
            next_progress = before + 10
            while time.monotonic() < deadline:
                current = state()
                if current['completed'] >= next_progress:
                    print(f'Page {index + 1} mode={mode}: {current["completed"] - before}/{rounds} rounds', flush=True)
                    next_progress += 10
                if current['completed'] >= target:
                    break
                receive(1)
            else:
                raise AssertionError('Autoplay round target timeout')
            key('U')
            final = idle()
            assert final['auto'] == 0 and final['completed'] >= target
            audit()
            key('o')
            key('O')
            assert state()['page'] == 0
            print(f'Page {index + 1} mode={mode}: acceptance PASS', flush=True)
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
        assert final['min'] > 8192 and final['stack'] > 512, final
        complete_log = log_path.read_text()
        assert not FAILURE.search(complete_log)
        if reset:
            assert complete_log.count('Odds Arcade ready:') == 1, 'Unexpected reset'
        dice = re.findall(r'dice count=(\d+) faces=([0-9,]+) total=(\d+) natural=(\d+)', complete_log)
        if 2 in games:
            assert dice
        for count, faces, total, _ in dice:
            values = [int(v) for v in faces.split(',')]
            assert len(values) == int(count) and all(1 <= v <= 6 for v in values)
            assert sum(values) == int(total)
        scores = re.findall(r'slot_score mode=(\d+) cells=([0-5]+) score=(\d+) total=(\d+) lines=(\d+) pairs=(\d+)', complete_log)
        totals = [0, 0]
        mode_counts = [0, 0]
        for mode, cells, score, total, lines, pairs in scores:
            mode, score, total, lines, pairs = map(int, (mode, score, total, lines, pairs))
            values = list(map(int, cells))
            pay = (20, 25, 35, 45, 60, 100)
            if mode == 0:
                assert len(values) == 3
                triple = len(set(values)) == 1
                pair = not triple and len(set(values)) == 2
                expected = pay[values[0]] if triple else 5 if pair else 0
                assert lines == int(triple) and pairs == int(pair)
            else:
                assert len(values) == 15
                paths = [(r, r, r) for r in range(3)] + [(0, 1, 2), (2, 1, 0),
                         (0, 1, 0), (1, 0, 1), (1, 2, 1), (2, 1, 2)]
                expected, hits = 0, 0
                for shape, path in enumerate(paths):
                    for start in range(3):
                        selected = [values[r * 5 + start + i] for i, r in enumerate(path)]
                        if len(set(selected)) == 1:
                            hits += 1
                            expected += pay[selected[0]] * (1 if shape < 3 else 2)
                pair_hits = sum(values[r * 5] == values[r * 5 + 1] for r in range(3)) if not hits else 0
                if not hits:
                    expected = pair_hits * 5
                assert hits == lines and pair_hits == pairs
            assert score == expected
            totals[mode] += score
            assert total == totals[mode]
            mode_counts[mode] += 1
        if 0 in games:
            assert all(n >= rounds for n in mode_counts), mode_counts
            assert final['total_classic'] == totals[0] and final['total_multi'] == totals[1]
        print(f'Slot scores validated: mode_counts={mode_counts}, totals={totals}', flush=True)
        drops = re.findall(r'plinko (?:ball=\d+ count=\d+ )?bin=(\d+) collisions=(\d+) ticks=(\d+) duration_ms=(\d+)', complete_log)
        bins = [0] * 9
        for bin_, collisions, _, duration in drops:
            bins[int(bin_) - 1] += 1
            assert int(collisions) > 0 and int(duration) < 10000
        if 3 in games:
            assert sum(bins[1:8]) >= rounds // 2 and sum(v > 0 for v in bins) >= 5, bins
        print(f'Device acceptance PASS: {final}; plinko_bins={bins}', flush=True)
        print(f'Local log: {log_path}', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--reset', action='store_true')
    parser.add_argument('--rounds', type=int, default=70)
    parser.add_argument('--games', nargs='+', choices=('slots', 'roulette', 'dice', 'plinko'),
                        default=('slots', 'roulette', 'dice', 'plinko'))
    args = parser.parse_args()
    assert 10 <= args.rounds <= 100
    names = ('slots', 'roulette', 'dice', 'plinko')
    run(args.port, args.log, args.rounds, args.reset, tuple(names.index(n) for n in args.games))
