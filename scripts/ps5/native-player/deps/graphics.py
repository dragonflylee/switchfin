#!/usr/bin/env python3
"""Build the pinned native OpenGL runtime with HDR presentation support."""
import argparse
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
from media import HERE, PINS, fetch, sha


def run(*args, **kwargs):
    subprocess.run(list(map(str, args)), check=True, **kwargs)


def checkout(name, destination, reference=None):
    pin = PINS[name]
    repository = reference
    if repository is None:
        repository = destination.with_name(destination.name + '-git')
        repository.mkdir()
        run('git', '-C', repository, 'init', '--quiet')
        run('git', '-C', repository, 'fetch', '--depth=1', pin['url'], pin['revision'])
    archive = subprocess.check_output(['git', '-C', str(repository), 'archive', pin['revision']])
    destination.mkdir()
    with tarfile.open(fileobj=io.BytesIO(archive)) as stream:
        stream.extractall(destination, filter='data')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prefix', 'work', 'cache', 'runtime'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--reference', type=Path, help='Read the pinned revision from an existing clone')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    prefix, work, cache, runtime = [getattr(args, name).resolve() for name in ('prefix', 'work', 'cache', 'runtime')]
    inputs = [HERE, runtime, cache]
    if args.reference:
        inputs.append(args.reference.resolve())
    def overlaps(a, b):
        return a == b or a.is_relative_to(b) or b.is_relative_to(a)
    if args.jobs < 1 or prefix.exists() or work.exists():
        parser.error('Use positive jobs and new output directories')
    if overlaps(prefix, work) or any(overlaps(output, path) for output in (prefix, work) for path in inputs):
        parser.error('Output overlaps an input directory')
    work.mkdir(parents=True)
    cache.mkdir(parents=True, exist_ok=True)
    source = work / 'source'
    checkout('graphics', source, args.reference)
    run('patch', '-Np1', '-i', HERE / 'graphics.patch', cwd=source)
    host = work / 'host'
    host.mkdir()
    for name in ('meson', 'pyyaml'):
        with tarfile.open(fetch(name, cache)) as stream:
            stream.extractall(host, filter='data')
    binaries = host / 'bin'
    binaries.mkdir()
    meson = binaries / 'meson'
    meson.write_text('#!/usr/bin/python3\nimport os, sys\nos.execv(sys.executable, [sys.executable, ' +
                     repr(str(host / 'meson-1.8.3/meson.py')) + ', *sys.argv[1:]])\n')
    meson.chmod(0o755)
    sdk = runtime / '.deps/native/ps5-payload-sdk'
    cross = work / 'native.ini'
    original = (sdk / 'toolchain/prospero.ini').read_text()
    if original.count('[target_machine]') != 1 or '[host_machine]' in original:
        raise ValueError('Unexpected SDK cross file')
    cross.write_text(original.replace('[target_machine]', '[host_machine]').replace('@DIRNAME@', str(sdk / 'toolchain')))
    environment = {'PATH': str(binaries) + ':/usr/bin:/bin', 'LC_ALL': 'C', 'LANG': 'C', 'TZ': 'UTC',
        'PYTHONNOUSERSITE': '1', 'PYTHONDONTWRITEBYTECODE': '1', 'SOURCE_DATE_EPOCH': '1788912000',
        'PYTHONPATH': str(host / 'pyyaml-6.0.2/lib'), 'LLVM_CONFIG': '/usr/bin/llvm-config-18',
        'PS5_PAYLOAD_SDK': str(sdk), 'PS5_NATIVE_APP_TEMPLATE': str(runtime), 'PS5_MESA_CROSS_FILE': str(cross),
        'PS5_SCANOUT_HEIGHT': '2160', 'PS5_SCANOUT_FPS': '60', 'PS5_GPU_PRESENT_BATCH': '1',
        'PS5_DEFERRED_DRAW_BATCH': '1', 'PSBC_JOBS': str(args.jobs)}
    if 'HOME' in os.environ:
        environment['HOME'] = os.environ['HOME']
    stages = [
        ['python3', 'tools/fetch-sources.py'],
        ['bash', 'toolchain/build-opengnm-psbc.sh'],
        ['make', 'test-compiler'],
        ['bash', 'toolchain/build-opengnm-psbc-ps5.sh'],
        ['bash', 'toolchain/build-mesa-ps5.sh'],
        ['bash', 'toolchain/install-ps5-opengl-core33.sh', prefix],
    ]
    for index, command in enumerate(stages):
        env = dict(environment)
        if index == 1:
            # The host compiler omits the optional NEON implementation; use its portable hash path.
            env.update(MAKEFLAGS='-e', CC='gcc-13 -DBLAKE3_USE_NEON=0', CXX='g++-13 -DBLAKE3_USE_NEON=0')
        print('Building graphics:', command[1], flush=True)
        with (work / f'{index + 1:02d}.log').open('w') as log:
            run(*command, cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT)
    licenses = prefix / 'share/licenses'
    shutil.copytree(source / 'LICENSES', licenses / 'components')
    for name in ('LICENSE', 'THIRD_PARTY_NOTICES.md'):
        shutil.copy2(source / name, licenses / (name.replace('.md', '.txt')))
    for name, filename in (('SPIRV-Headers', 'LICENSE'), ('Vulkan-Headers', 'LICENSE.md')):
        shutil.copy2(source / 'third_party' / name / filename, licenses / (name + '.txt'))
    (prefix / 'graphics.json').write_text(json.dumps({'source': PINS['graphics'],
        'patch_sha256': sha(HERE / 'graphics.patch'), 'width': 3840, 'height': 2160, 'refresh_hz': 60,
        'libraries': {p.name: sha(p) for p in sorted((prefix / 'lib').glob('*')) if p.is_file()}}, indent=2) + '\n')
    print(prefix)


if __name__ == '__main__':
    main()
