#!/usr/bin/env python3
"""Prepare the pinned native CRT, C++ support, packager and runtime module."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import zipfile

HERE = Path(__file__).resolve().parent
REVISION = '722f2227a8bb6fa2229120546995b6562552c752'
SDK_SHA = '8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da'


def run(*args, **kwargs):
    return subprocess.run(list(map(str, args)), check=True, **kwargs)


def prepare(args):
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    template = args.template
    if template is None:
        template = out / 'template-repository'
        run('git', 'clone', '--no-checkout', 'https://github.com/BlackBearReloaded/ps5-native-app-boilerplate.git', template)
    archive = run('git', '-C', template, 'archive', REVISION, capture_output=True).stdout
    with tarfile.open(fileobj=io.BytesIO(archive)) as source:
        source.extractall(out, filter='data')
    run('git', 'apply', HERE / 'native-cxx.patch', cwd=out)
    if args.sdk_archive:
        data = args.sdk_archive.read_bytes()
        if hashlib.sha256(data).hexdigest() != SDK_SHA:
            raise ValueError('SDK archive checksum mismatch')
        cache = out / '.deps/native'
        cache.mkdir(parents=True)
        shutil.copy2(args.sdk_archive, cache / 'ps5-payload-sdk.zip')
        with zipfile.ZipFile(io.BytesIO(data)) as sdk_zip:
            for entry in sdk_zip.infolist():
                target = (cache / entry.filename).resolve()
                if not target.is_relative_to(cache):
                    raise ValueError('Invalid SDK archive path')
                sdk_zip.extract(entry, cache)
                mode = entry.external_attr >> 16
                if mode and target.is_file():
                    target.chmod(mode & 0o777)
    run('bash', out / 'tools/setup-native-dependencies.sh', cwd=out)
    # The template reproduces its open-source libc import module and checks its hash.
    run('bash', out / 'tools/rebuild-libc.sh', cwd=out)
    zlib = out / '.deps/native/zlib/root'
    native = out / 'tooling/native'
    sdk = out / '.deps/native/ps5-payload-sdk'
    host, objects = out / 'build/host', out / 'build/obj'
    host.mkdir(parents=True, exist_ok=True)
    objects.mkdir(parents=True, exist_ok=True)
    run('clang++-18', '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-I', zlib / 'usr/include',
        *(native / name for name in ('native_app_builder.cpp', 'self_container.cpp', 'elf_object.cpp', 'sce_module_writer.cpp')),
        next(zlib.rglob('libz.a')), '-o', host / 'ps5-native-tool')
    environment = dict(os.environ, PS5_PAYLOAD_SDK=str(sdk))
    compiler = out / 'tooling/prospero-clang18'
    compiler.chmod(0o755)
    run('sh', compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-fno-exceptions', '-fno-rtti',
        '-ffunction-sections', '-fdata-sections', '-c', native / 'app_crt.cpp', '-o', objects / 'app_crt.o', env=environment)
    support = out / 'vendor/libc-objects'
    support.mkdir(parents=True)
    names = ('assert.o', 'no-locale.o', 'mbsnrtowcs.o', 'wcsnrtombs.o')
    run('llvm-ar-18', 'x', sdk / 'target/lib/libc.a', *names, cwd=support)
    run('sh', compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-ffunction-sections', '-fdata-sections',
        '-c', HERE / 'emutls.c', '-o', support / 'emutls.o', env=environment)
    run('llvm-ar-18', 'rcs', out / 'vendor/native-support.a', names[0], 'emutls.o', *names[1:], cwd=support)
    files = ('build/obj/app_crt.o', 'build/host/ps5-native-tool', 'vendor/native-support.a', 'runtime/libc.prx',
             'tooling/native/ps5-pie.ld', 'tooling/native/app-symbols.map', 'tooling/prospero-clang18')
    (out / 'runtime.json').write_text(json.dumps({'template': REVISION, 'sdk_sha256': SDK_SHA,
        'files': {name: hashlib.sha256((out / name).read_bytes()).hexdigest() for name in files}}, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--template', type=Path, help='Local clone containing the pinned upstream revision')
    parser.add_argument('--sdk-archive', type=Path, help='Cached checksum-verified SDK archive')
    prepare(parser.parse_args())
