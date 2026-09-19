#!/usr/bin/env python3
"""Prepare the pinned runtime, graphics, SDL and media dependencies for PS5."""
import argparse
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True, help='New directory for the complete dependency build')
    parser.add_argument('--cache', type=Path, required=True, help='Reusable public source download cache')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    out, cache = args.out.resolve(), args.cache.resolve()
    if args.jobs < 1 or out.exists():
        parser.error('Use positive jobs and a new output directory')
    if out == cache or out.is_relative_to(cache) or cache.is_relative_to(out):
        parser.error('Keep the source cache separate from the build output')
    if out == HERE or HERE.is_relative_to(out) or out.is_relative_to(HERE):
        parser.error('Build output overlaps the recipes')
    out.mkdir(parents=True)
    cache.mkdir(parents=True, exist_ok=True)
    runtime, graphics, sdl, media = [out / name for name in ('runtime', 'graphics', 'sdl', 'media')]
    commands = [
        [HERE / 'runtime/prepare.py', '--out', runtime],
        [HERE / 'deps/graphics.py', '--prefix', graphics, '--work', out / 'graphics-build',
         '--cache', cache, '--runtime', runtime, '--jobs', args.jobs],
        [HERE / 'deps/sdl.py', '--prefix', sdl, '--work', out / 'sdl-build',
         '--runtime', runtime, '--graphics', graphics, '--jobs', args.jobs],
        [HERE / 'deps/media.py', '--prefix', media, '--work', out / 'media-build',
         '--cache', cache, '--runtime', runtime, '--graphics', graphics, '--sdl', sdl, '--jobs', args.jobs],
    ]
    for command in commands:
        subprocess.run([sys.executable, '-B', *map(str, command)], check=True)
    print('Native dependencies:', out)


if __name__ == '__main__':
    main()
