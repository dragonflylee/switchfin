#!/usr/bin/env python3
"""Build SDL audio, input and EGL services for the native PS5 application."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
from graphics import checkout
from media import HERE, PINS, sha


def run(*args, **kwargs):
    subprocess.run(list(map(str, args)), check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prefix', 'work', 'runtime', 'graphics'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--reference', type=Path, help='Read the pinned SDL revision from an existing clone')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    prefix, work, runtime, gl = [getattr(args, name).resolve() for name in ('prefix', 'work', 'runtime', 'graphics')]
    inputs = [HERE, runtime, gl]
    if args.reference:
        inputs.append(args.reference.resolve())
    def overlaps(a, b):
        return a == b or a.is_relative_to(b) or b.is_relative_to(a)
    if args.jobs < 1 or prefix.exists() or work.exists():
        parser.error('Use positive jobs and new output directories')
    if overlaps(prefix, work) or any(overlaps(output, path) for output in (prefix, work) for path in inputs):
        parser.error('Output overlaps an input directory')
    work.mkdir(parents=True)
    source, build = work / 'source', work / 'cmake'
    checkout('sdl', source, args.reference)
    run('patch', '-Np1', '-i', HERE / 'sdl/sdl.patch', cwd=source)
    sdk = runtime / '.deps/native/ps5-payload-sdk'
    env = dict(os.environ, PS5_PAYLOAD_SDK=str(sdk), PS5_CLANG='/usr/bin/clang-18')
    compiler = runtime / 'tooling/prospero-clang18'
    run('cmake', '-S', HERE / 'sdl', '-B', build, '-G', 'Ninja',
        '-DSDL_SOURCE=' + str(source), '-DPS5_OPENGL_PREFIX=' + str(gl),
        '-DCMAKE_SYSTEM_NAME=Generic', '-DCMAKE_SYSTEM_PROCESSOR=x86_64',
        '-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY', '-DCMAKE_C_COMPILER=' + str(compiler),
        '-DCMAKE_CXX_COMPILER=' + str(compiler), '-DCMAKE_INSTALL_PREFIX=' + str(prefix),
        '-DCMAKE_AR=/usr/bin/ar', '-DCMAKE_RANLIB=/usr/bin/ranlib', '-DCMAKE_LINKER=/usr/bin/ld.lld',
        '-DCMAKE_INSTALL_LIBDIR=lib', '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_C_FLAGS=-D__PROSPERO__ -fPIC -ffunction-sections -fdata-sections -ffile-prefix-map=' + str(work) + '=.',
        env=env)
    run('cmake', '--build', build, '--parallel', args.jobs, env=env)
    run('cmake', '--install', build, env=env)
    obj = work / 'common-dialog.o'
    run(compiler, '-std=c11', '-O2', '-fPIC', '-ffunction-sections', '-fdata-sections',
        '-c', HERE / 'sdl/common_dialog_link_stub.c', '-o', obj, env=env)
    run('ld.lld', '--shared', '-soname', 'libSceCommonDialog.prx', '-o', prefix / 'lib/libSceCommonDialog.so', obj)
    licenses = prefix / 'share/licenses'
    licenses.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source / 'LICENSE.txt', licenses / 'SDL.txt')
    shutil.copy2(runtime / 'LICENSE', licenses / 'Native-integration.txt')
    (prefix / 'native-sdl.json').write_text(json.dumps({'source': PINS['sdl'],
        'patch_sha256': sha(HERE / 'sdl/sdl.patch'), 'library_sha256': sha(prefix / 'lib/libSDL2.a'),
        'audio_policy': '48000-stereo-surround-opt-in',
        'audio_source_sha256': {p.name: sha(p) for p in sorted((source / 'src/audio/ps5').glob('*')) if p.is_file()}
    }, indent=2) + '\n')
    print(prefix)


if __name__ == '__main__':
    main()
