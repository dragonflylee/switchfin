#!/usr/bin/env python3
"""Generate native display, audio and translation headers during configuration."""
import argparse
import json
import hashlib
from pathlib import Path
import re


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def configure(source, build, graphics, sdl):
    for name in ('include/ps5_opengl_display.h', 'lib/libPS5OpenGLCore33.a'):
        if not (graphics / name).is_file():
            raise ValueError('Missing native graphics input: ' + name)
    audio = json.loads((sdl / 'native-sdl.json').read_text())
    if (audio.get('source', {}).get('revision') != '8c56053f13ca13a0c050de613706ff69eb615836'
            or audio.get('audio_policy') != '48000-stereo-surround-opt-in'
            or audio.get('library_sha256') != hashlib.sha256((sdl / 'lib/libSDL2.a').read_bytes()).hexdigest()):
        raise ValueError('SDL does not provide the required native audio policy')
    token = 'SWITCHFIN_AUDIO_POLICY_V1:' + audio['audio_policy']
    write(build / 'native-audio/ps5_native_audio_config.hpp',
          '#pragma once\n#define PS5_NATIVE_AUDIO_51_AVAILABLE 1\n'
          '#define PS5_NATIVE_AUDIO_BUILD_TOKEN ' + json.dumps(token) + '\n')
    write(build / 'native-display/ps5_native_display_config.hpp', '''#pragma once
#define PS5_NATIVE_DISPLAY_WIDTH 3840
#define PS5_NATIVE_DISPLAY_HEIGHT 2160
#define PS5_NATIVE_DISPLAY_REFRESH_HZ 60
#include <ps5_opengl_display.h>
#if PS5_OPENGL_NATIVE_WIDTH != PS5_NATIVE_DISPLAY_WIDTH || PS5_OPENGL_NATIVE_HEIGHT != PS5_NATIVE_DISPLAY_HEIGHT || PS5_OPENGL_NATIVE_FPS != PS5_NATIVE_DISPLAY_REFRESH_HZ
#error Native graphics and application display dimensions differ
#endif
''')
    entries = []
    for path in sorted((source / 'resources/i18n').glob('*/*.json')):
        locale, filename = path.parts[-2:]
        if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]*', locale) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*\.json', filename):
            raise ValueError('Invalid translation filename: ' + str(path))
        if not isinstance(json.loads(path.read_text(encoding='utf-8-sig')), dict):
            raise ValueError('Translation must contain an object: ' + str(path))
        entries.append('    {' + json.dumps(locale) + ', ' + json.dumps(filename) + '},\n')
    if not (source / 'resources/i18n/en-US/main.json').is_file():
        raise ValueError('Missing default translation')
    write(build / 'native-i18n/ps5_native_i18n_catalog.hpp',
          '#pragma once\nnamespace brls::ps5_native_i18n {\n'
          'struct CatalogEntry { const char* locale; const char* filename; };\n'
          'inline constexpr CatalogEntry catalog[] = {\n' + ''.join(entries) + '};\n}\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'build', 'graphics', 'sdl'):
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    configure(args.source, args.build, args.graphics, args.sdl)
