#!/usr/bin/env python3
"""Run libgbasave's historical script-style regression suite.

The suite predates a single test framework, so each test_*.py file is executed as
its own process. A non-zero exit is a failure; fixture-unavailable tests print SKIP
and exit successfully.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--gbasavehandler-bin', type=Path)
    args = parser.parse_args()

    root = args.root.resolve()
    workspace = root.parent
    consumer = args.gbasavehandler_bin or workspace / 'build/GBASaveHandler/GBASaveHandler'

    env = os.environ.copy()
    env['GBASAVEHANDLER_BIN'] = str(consumer)
    env.setdefault('TERM', 'xterm')

    tests = sorted(p for p in (root / 'tests').glob('test_*.py') if p.name != 'test_support.py')
    failures: list[str] = []
    for test in tests:
        print(f'=== {test.name}', flush=True)
        result = subprocess.run([sys.executable, str(test)], cwd=root, env=env)
        if result.returncode != 0:
            failures.append(test.name)

    print(f'\nlibgbasave regression scripts: {len(tests) - len(failures)}/{len(tests)} passed')
    if failures:
        print('FAILED: ' + ', '.join(failures))
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
