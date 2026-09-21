#!/usr/bin/env python3
"""Build and package Switchfin with prepared PS5 native dependencies."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import zipfile
import socket_adapter
from verify_layout import verify_layout

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
WRAPS = ('malloc', 'calloc', 'realloc', 'free', 'posix_memalign', 'malloc_usable_size',
         'sceLibcMspaceMalloc', 'sceLibcMspaceCalloc', 'sceLibcMspaceRealloc', 'sceLibcMspacePosixMemalign',
         'Curl_pipe', 'Curl_thread_create', 'pthread_create')


def run(*args, **kwargs):
    subprocess.run(list(map(str, args)), check=True, **kwargs)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def package_resources(folder, runtime, media, sdl, graphics):
    (folder / 'sce_sys').mkdir(parents=True)
    (folder / 'sce_module').mkdir()
    shutil.copy2(ROOT / 'app/platform/ps5/param.json', folder / 'sce_sys/param.json')
    for name in ('icon0.png', 'pic1.png'):
        shutil.copy2(ROOT / 'app/platform/ps5/sce_sys' / name, folder / 'sce_sys' / name)
    for name in ('pic0.dds', 'pic1.dds'):
        shutil.copy2(ROOT / 'app/platform/ps5/sce_sys/jellyfin-background.dds', folder / 'sce_sys' / name)
    module = runtime / 'runtime/libc.prx'
    if digest(module) != 'e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036':
        raise ValueError('Native runtime module does not match the pinned template')
    shutil.copy2(module, folder / 'sce_module/libc.prx')
    shutil.copytree(ROOT / 'resources', folder / 'resources')
    shutil.copy2(media / 'share/native-media/ca-bundle.crt', folder / 'ca-bundle.crt')
    shutil.copytree(media / 'share/native-media/fontconfig', folder / 'resources/fontconfig')
    licenses = folder / 'licenses'
    licenses.mkdir()
    for name, sources in {
        'Switchfin.txt': ['LICENSE', 'app/platform/ps5/NOTICE.txt',
                          'scripts/ps5/native-player/posix/LICENSE.musl'],
        'Borealis.txt': ['library/borealis/LICENSE', 'library/borealis/library/lib/extern/tweeny/LICENSE'],
        'lunasvg.txt': ['library/lunasvg/LICENSE', 'library/lunasvg/plutovg/LICENSE',
                        'library/lunasvg/plutovg/source/FTL.TXT'],
        'fmt.txt': ['library/borealis/library/lib/extern/fmt/LICENSE'],
        'yoga.txt': ['library/borealis/library/lib/extern/yoga/LICENSE'],
        'NanoVG.txt': ['library/borealis/library/include/borealis/extern/nanovg/LICENSE.txt'],
    }.items():
        (licenses / name).write_text('\n\n'.join((ROOT / path).read_text() for path in sources))
    shutil.copy2(runtime / 'LICENSE', licenses / 'Native-runtime.txt')
    for name in ('LICENSE.emutls', 'NOTICE.emutls'):
        shutil.copy2(HERE / 'runtime' / name, licenses / name)
    shutil.copytree(graphics / 'share/licenses', licenses / 'graphics')
    # The native media recipe retains the notices of its static dependencies.
    shutil.copytree(media / 'share/licenses', licenses / 'media')
    shutil.copytree(sdl / 'share/licenses', licenses / 'sdl')


def build(args):
    out, runtime, media, sdl, graphics = [path.resolve() for path in
        (args.out, args.runtime, args.media, args.sdl, args.graphics)]
    for protected in (ROOT / 'app', ROOT / 'library', ROOT / 'resources', ROOT / 'scripts', runtime, media, sdl, graphics):
        if out == protected or out.is_relative_to(protected) or protected.is_relative_to(out):
            raise ValueError('Output overlaps an input directory: ' + str(protected))
    out.mkdir(parents=True, exist_ok=False)
    sdk = runtime / '.deps/native/ps5-payload-sdk'
    environment = dict(os.environ, PS5_PAYLOAD_SDK=str(sdk))
    cmake = args.cmake_dir.resolve() if args.cmake_dir else out / 'cmake'
    run('cmake', '-S', ROOT, '-B', cmake, '-DPLATFORM_PS5=ON', '-DCMAKE_BUILD_TYPE=Release',
        '-DPS5_NATIVE_RUNTIME=' + str(runtime), '-DPS5_NATIVE_PREFIX=' + str(media),
        '-DPS5_NATIVE_SDL_PREFIX=' + str(sdl), '-DPS5_NATIVE_GL_PREFIX=' + str(graphics), env=environment)
    run('cmake', '--build', cmake, '-j', args.jobs, env=environment)
    manifest = json.loads((cmake / 'native-link.json').read_text())
    app = [Path(path) for path in manifest['archives']]
    if any(not path.resolve().is_relative_to(cmake) for path in app):
        raise ValueError('Application archive is outside its build directory')
    libraries = sorted((media / 'lib').glob('*.a'))
    forbidden = {'libc.a', 'libc++.a', 'libc++abi.a', 'libunwind.a', 'libSDL2.a', 'libSDL2main.a', 'libGL.a'}
    if not libraries or any(path.name in forbidden or 'OSMesa' in path.name for path in libraries):
        raise ValueError('Native media prefix contains an incompatible runtime')
    socket, _ = socket_adapter.prepare(out, media)
    objects = [socket]
    for source in ('native_heap.c', 'unsupported_services.c'):
        obj = out / (source + '.o')
        run('sh', runtime / 'tooling/prospero-clang18', '-std=c11', '-O2', '-ffunction-sections',
            '-fdata-sections', '-I', ROOT / 'app/include', '-c', HERE / source, '-o', obj, env=environment)
        objects.append(obj)
    packager = runtime / 'build/host/ps5-native-tool'
    imports = sorted((sdk / 'target/lib').glob('*.so'))
    imports += [sdl / 'lib/libSceCommonDialog.so', graphics / 'lib/libSceAgc.so', graphics / 'lib/libSceAgcDriver.so']
    linked = out / 'llvm-pie.elf'
    run(sdk / 'bin/prospero-lld', '-T', runtime / 'tooling/native/ps5-pie.ld', '--eh-frame-hdr',
        '--gc-sections', '--error-limit=0', '--defsym=__cxa_thread_atexit_impl=0',
        '--defsym=_ZTH23_mesa_glapi_tls_Context=0', '--defsym=ZSTD_trace_decompress_begin=0',
        '--defsym=ZSTD_trace_decompress_end=0', '-u', 'ps5_agc_gate2_run',
        *('--wrap=' + name for name in WRAPS), '--version-script', runtime / 'tooling/native/app-symbols.map',
        '-e', '_start', '-o', linked, runtime / 'build/obj/app_crt.o', *objects,
        '-L', graphics / 'lib', '-L', sdk / 'target/lib', '--start-group', *app, *libraries,
        sdl / 'lib/libSDL2.a', graphics / 'lib/libPS5OpenGLCore33.a',
        *(sdk / 'target/lib' / name for name in ('libc++.a', 'libc++abi.a', 'libunwind.a')),
        runtime / 'vendor/native-support.a', '--end-group', '--as-needed', *imports, env=environment)
    stubs = out / 'imports'
    stubs.mkdir()
    for path in imports:
        shutil.copy2(path, stubs / path.name)
    eboot = out / 'eboot.elf'
    run(packager, 'link', '--in', linked, '--out', eboot, '--stub-dir', stubs,
        '--module-sdk', '0x02000009', '--companion-sdk', '0x08050001', '--file-name', 'eboot.elf')
    layout = verify_layout(linked, eboot)
    param = json.loads((ROOT / 'app/platform/ps5/param.json').read_text())
    folder = out / 'dist' / param['titleId']
    package_resources(folder, runtime, media, sdl, graphics)
    run(packager, 'self', '--sign', '--in', eboot, '--out', folder / 'eboot.bin', '--magic', '0x1D3D154F')
    files = {path.relative_to(folder).as_posix(): digest(path) for path in sorted(folder.rglob('*')) if path.is_file()}
    with zipfile.ZipFile(out / 'Switchfin.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for name in files:
            archive.write(folder / name, folder.name + '/' + name)
    (out / 'build.json').write_text(json.dumps({'source': subprocess.check_output(
        ['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(), 'files': files, 'layout': layout}, indent=2) + '\n')
    print(out / 'Switchfin.zip')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('out', 'runtime', 'media', 'sdl', 'graphics'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=6)
    parser.add_argument('--cmake-dir', type=Path, help='Reuse an isolated CMake build directory')
    build(parser.parse_args())
