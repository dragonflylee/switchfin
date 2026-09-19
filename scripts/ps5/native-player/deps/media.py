#!/usr/bin/env python3
"""Build the native media libraries from pinned sources and SDK inputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
from urllib.parse import urlsplit

HERE = Path(__file__).resolve().parent
PINS = json.loads((HERE / 'pins.json').read_text())
LIBRARIES='archive ass avcodec avdevice avfilter avformat avutil b64 bz2 charset curl deflate expat fmt freetype fribidi gif harfbuzz harfbuzz-subset iconv jansson jpeg json-c lzma m minizip openlibm png png16 postproc psl samplerate sharpyuv ssh2 swresample swscale tinyxml2 turbojpeg webp webpdemux webpmux x264 xml2 z zstd'.split()
EXCLUDE_HEADERS={'AL','FLAC','FLAC++','GL','KHR','EGL','GLES','GLES2','GLES3','GLFW','SDL2','RmlUi','llvm','llvm-c','clang','kitchensink2','mpv','openssl','fontconfig'}
SUPPORT=['gmtime.o','localtime.o','nl_langinfo.o','regcomp.o','regerror.o','regexec.o','regfree.o']

def sha(path):
    with Path(path).open('rb') as f: return hashlib.file_digest(f,'sha256').hexdigest()

def inventory(root):
    if any(p.is_symlink() for p in root.rglob('*')):
        raise RuntimeError(f'prefix inventory cannot contain symlinks: {root}')
    return {p.relative_to(root).as_posix():{'bytes':p.stat().st_size,'sha256':sha(p)}
            for p in sorted(root.rglob('*')) if p.is_file() and p.name!='receipt.json'}

def tree_id(files):
    return hashlib.sha256(json.dumps(files,sort_keys=True,separators=(',',':')).encode()).hexdigest()

def run(args,*,cwd=None,env=None,log=None):
    values=list(map(str,args))
    if log:
        with open(log,'w') as output:
            output.write(json.dumps(values)+'\n'); output.flush()
            result=subprocess.run(values,cwd=cwd,env=env,stdout=output,stderr=subprocess.STDOUT)
        if result.returncode:
            print(Path(log).read_text()[-7000:])
            raise subprocess.CalledProcessError(result.returncode,values)
    else: subprocess.run(values,cwd=cwd,env=env,check=True)

def atomic_json(path,value):
    temp=path.with_suffix('.new')
    temp.write_text(json.dumps(value,indent=2,sort_keys=True)+'\n')
    temp.replace(path)

def copy_headers(payload, prefix):
    result={}
    for source in sorted((payload/'include').rglob('*')):
        rel=source.relative_to(payload/'include')
        if rel.parts[0] in EXCLUDE_HEADERS or rel.name.startswith(('imgui','imstb','imconfig')) or not source.is_file(): continue
        target=prefix/'include'/rel
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,target)
        result[rel.as_posix()]={'bytes':source.stat().st_size,'sha256':sha(source)}
    return result

def copy_metadata(payload,prefix):
    result={}
    for source in sorted((payload/'lib/pkgconfig').glob('*.pc')):
        if source.name in {'mpv.pc','openssl.pc','libssl.pc','libcrypto.pc','fontconfig.pc','osmesa.pc'} or source.name.startswith('SDL'): continue
        value=source.read_text()
        # Keep only metadata whose referenced archives were actually copied.
        libs=re.findall(r'(?<!\S)-l([A-Za-z0-9_+.-]+)',value)
        if any(name not in LIBRARIES and name not in {'ssl','crypto','fontconfig','pthread'} for name in libs): continue
        result[source.name]=sha(source)
        value=value.replace('/opt/ps5-payload-sdk/target/user/homebrew',str(prefix)).replace('/user/homebrew',str(prefix))
        (prefix/'lib/pkgconfig'/source.name).write_text(value)
    return result

def fetch(name,cache):
    pin=PINS[name]; suffix=Path(urlsplit(pin['url']).path).name
    path=cache/suffix
    if path.is_file() and sha(path)==pin['sha256']: return path
    temp=path.with_suffix(path.suffix+'.partial')
    run(['curl','--fail','--location','--retry','2','--output',temp,pin['url']])
    if sha(temp)!=pin['sha256']: raise RuntimeError(f'{name}: source digest mismatch')
    temp.replace(path); return path

def extract(name,cache,work):
    archive=fetch(name,cache)
    source=work/f'{name}-{PINS[name]["version"]}'
    if not source.exists():
        with tarfile.open(archive) as stream: stream.extractall(work,filter='data')
    return source

def write_tools(prefix,work,runtime,sdl,gl):
    tools=work/'tools'; tools.mkdir(exist_ok=True)
    script='''#!/usr/bin/env python3
import os,sys
from pathlib import Path
base=Path(RUNTIME)
sdk=base/'.deps/native/ps5-payload-sdk'
prefix=Path(PREFIX)
args=sys.argv[1:]
compile_only=any(a in args for a in ('-c','-S','-E','--version','-v','-dumpmachine','-dumpversion'))
common=['-fPIC','-ffunction-sections','-fdata-sections','-ffile-prefix-map='+WORK+'=native-media-build']
extra=[]
if not compile_only:
    extra=['-nostdlib','-Wl,-T,'+str(base/'tooling/native/ps5-pie.ld'),'-Wl,-e,main','-Wl,--gc-sections',
      '-L'+str(prefix/'lib'),'-L'+str(sdk/'target/lib'),'-Wl,--start-group',str(prefix/'lib/libswitchfin-native-c.a'),str(base/'vendor/native-support.a'),
      *[str(sdk/'target/lib'/n) for n in ('libc++.a','libc++abi.a','libunwind.a')],'-Wl,--end-group','-Wl,--as-needed',
      *map(str,sorted((sdk/'target/lib').glob('*.so')))]
os.environ['PS5_PAYLOAD_SDK']=str(sdk)
os.environ['PATH']=str(sdk/'bin')+':'+os.environ['PATH']
os.execv('/bin/sh',['sh',str(base/'tooling/prospero-clang18'),*common,*args,*extra])
'''.replace('RUNTIME',repr(str(runtime))).replace('PREFIX',repr(str(prefix))).replace('WORK',repr(str(work)))
    (tools/'cc').write_text(script); (tools/'cc').chmod(0o755)
    pkg='''#!/usr/bin/env python3
import os,sys
os.environ['PKG_CONFIG_LIBDIR']=@PKG_LIBDIR@
os.environ.pop('PKG_CONFIG_PATH',None)
os.environ.pop('PKG_CONFIG_SYSROOT_DIR',None)
os.execv('/usr/bin/pkg-config',['pkg-config',*sys.argv[1:]])
'''.replace('@PKG_LIBDIR@',repr(':'.join(str(p/'lib/pkgconfig') for p in (prefix,sdl,gl))))
    (tools/'pkg-config').write_text(pkg); (tools/'pkg-config').chmod(0o755)
    return tools

def bootstrap(prefix,work,runtime,payload):
    if (work/'bootstrap.json').exists(): return
    marker=prefix/'.switchfin-native-media'
    if prefix.exists() and any(prefix.iterdir()) and not marker.is_file(): raise RuntimeError('native prefix must be new')
    for directory in ('include','lib/pkgconfig','share/licenses','share/native-media'):
        (prefix/directory).mkdir(parents=True,exist_ok=True)
    marker.write_text('Switchfin isolated native dependency prefix\n')
    headers=copy_headers(payload,prefix)
    expected=json.loads((HERE/'reused-inputs.json').read_text())
    if tree_id(headers)!=expected['headers_inventory_sha256']: raise RuntimeError('reused header tree changed')
    libs={}
    for name in LIBRARIES:
        source=payload/'lib'/f'lib{name}.a'
        if sha(source)!=expected['libraries'][source.name]: raise RuntimeError(f'reused archive changed: {name}')
        shutil.copy2(source,prefix/'lib'/source.name); libs[source.name]=sha(source)
    metadata=copy_metadata(payload,prefix)
    objects=work/'portable'; objects.mkdir(exist_ok=True)
    sdk=runtime/'.deps/native/ps5-payload-sdk'
    run(['llvm-ar-18','x',sdk/'target/lib/libc.a',*SUPPORT],cwd=objects)
    support={p.name:sha(p) for p in objects.glob('*.o')}
    if support!=expected['portable_objects']: raise RuntimeError('Native portable support changed')
    run(['llvm-ar-18','rcs',prefix/'lib/libswitchfin-native-c.a',*sorted(objects.glob('*.o'))])
    atomic_json(work/'bootstrap.json',{'reused_headers':headers,'reused_libraries':libs,'metadata_original_sha256':metadata,'portable_objects':support})
    atomic_json(prefix/'share/native-media/copied-inputs.json',json.loads((work/'bootstrap.json').read_text()))

def openssl(prefix,work,cache,env,jobs):
    src=extract('openssl',cache,work)
    if not (src/'Makefile').exists():
        run(['perl','Configure','BSD-x86_64','no-shared','no-dso','no-async','no-tests','no-module',
             f'--prefix={prefix}','--libdir=lib','--openssldir=/download0/ssl'],cwd=src,env=env,log=work/'openssl-configure.log')
    run(['make',f'-j{jobs}','build_libs'],cwd=src,env=env,log=work/'openssl-build.log')
    # install_dev copies headers/static libraries and pkg-config without apps.
    run(['make','install_dev'],cwd=src,env=env,log=work/'openssl-install.log')
    shutil.copy2(src/'LICENSE.txt',prefix/'share/licenses/OpenSSL.txt')
    atomic_json(work/'openssl.json',{'config':'BSD-x86_64 no-shared no-dso no-async no-tests no-module',
        'configdata_sha256':sha(src/'configdata.pm'),'archives':{n:sha(prefix/'lib'/n) for n in ('libssl.a','libcrypto.a')}})

def fontconfig(prefix,work,cache,env,jobs,tools):
    gperf=extract('gperf',cache,work)
    host=work/'host'
    if not (host/'bin/gperf').exists():
        hostenv=dict(os.environ)
        for name in ('CC','CXX','CFLAGS','CXXFLAGS','LDFLAGS','CPPFLAGS','PKG_CONFIG'): hostenv.pop(name,None)
        run(['./configure',f'--prefix={host}'],cwd=gperf,env=hostenv,log=work/'gperf-configure.log')
        run(['make',f'-j{jobs}'],cwd=gperf,env=hostenv,log=work/'gperf-build.log')
        run(['make','install'],cwd=gperf,env=hostenv,log=work/'gperf-install.log')
    src=extract('fontconfig',cache,work)
    if not (src/'.native-link-probes').exists():
        meson=src/'meson.build'; value=meson.read_text()
        old="check_header_symbols = [\n  ['posix_fadvise', 'fcntl.h']\n]"
        if value.count(old)!=1: raise RuntimeError('Fontconfig posix_fadvise probe changed')
        meson.write_text(value.replace(old,"# Native SDK declarations do not guarantee runtime exports.\ncheck_funcs += [['posix_fadvise']]\ncheck_header_symbols = []"))
        (src/'.native-link-probes').write_text('1\n')
    build=work/'fontconfig-build'
    cross=work/'fontconfig-cross.ini'
    cross.write_text(f"[binaries]\nc = '{tools/'cc'}'\nar = 'llvm-ar-18'\nstrip = 'llvm-strip-18'\npkgconfig = '{tools/'pkg-config'}'\n[host_machine]\nsystem = 'freebsd'\ncpu_family = 'x86_64'\ncpu = 'x86_64'\nendian = 'little'\n[properties]\nneeds_exe_wrapper = true\n")
    buildenv=dict(env,PATH=str(host/'bin')+':'+env['PATH'])
    if (build/'build.ninja').exists():
        run(['python3','-m','mesonbuild.mesonmain','setup','--reconfigure',build,src],
            env=buildenv,log=work/'fontconfig-reconfigure.log')
    if not (build/'build.ninja').exists():
        run(['python3','-m','mesonbuild.mesonmain','setup',build,src,'--cross-file',cross,
            f'--prefix={prefix}','--libdir=lib','--default-library=static','--buildtype=release','--wrap-mode=nofallback',
            '-Ddoc=disabled','-Dtests=disabled','-Dtools=disabled','-Dcache-build=disabled','-Dnls=disabled',
            '-Dcache-dir=/download0/cache/fontconfig','-Dbaseconfig-dir=/app0/resources/fontconfig',
            '-Dconfig-dir=/app0/resources/fontconfig/conf.d','-Ddefault-fonts-dirs=/app0/resources/font',
            '-Dadditional-fonts-dirs='],env=buildenv,log=work/'fontconfig-configure.log')
    config=(build/'config.h').read_text()
    if '#define HAVE_MKOSTEMP 1' in config: raise RuntimeError('native Fontconfig incorrectly selected mkostemp')
    if '#define HAVE_MKSTEMP 1' not in config: raise RuntimeError('native Fontconfig needs real mkstemp')
    if '#define HAVE_POSIX_FADVISE 1' in config: raise RuntimeError('native Fontconfig unexpectedly requires posix_fadvise')
    run(['ninja','-C',build,f'-j{jobs}'],env=buildenv,log=work/'fontconfig-build.log')
    # Fontconfig's target configuration paths are absolute. Stage installation
    # privately so Meson never writes /app0 or another directory outside work.
    stage=work/'fontconfig-install'
    stage.mkdir(exist_ok=True)
    run(['python3','-m','mesonbuild.mesonmain','install','-C',build,'--no-rebuild'],
        env=dict(buildenv,DESTDIR=str(stage)),log=work/'fontconfig-install.log')
    shutil.copytree(stage/str(prefix).lstrip('/'),prefix,dirs_exist_ok=True)
    config_destination=prefix/'share/native-media/fontconfig'
    config_destination.mkdir(exist_ok=True)
    shutil.copy2(stage/'app0/resources/fontconfig/fonts.conf',config_destination/'fonts.conf')
    (config_destination/'conf.d').mkdir(exist_ok=True)
    for entry in sorted((stage/'app0/resources/fontconfig/conf.d').iterdir()):
        # The staged install links absolute target paths. Copy the pinned
        # conf.avail files as ordinary files for raw-folder packaging.
        source=prefix/'share/fontconfig/conf.avail'/entry.name if entry.is_symlink() else entry
        shutil.copy2(source,config_destination/'conf.d'/entry.name)
    shutil.copy2(src/'COPYING',prefix/'share/licenses/Fontconfig.txt')
    atomic_json(work/'fontconfig.json',{'mkostemp':False,'mkstemp':True,'posix_fadvise':False,
        'native_meson_sha256':sha(src/'meson.build'),'config_sha256':sha(build/'config.h'),'archive_sha256':sha(prefix/'lib/libfontconfig.a')})

def mpv(prefix,work,cache,env,jobs):
    src=extract('mpv',cache,work)
    if not (src/'.native-patched').exists():
        run(['patch','-Np1','-i',HERE/'mpv.patch'],cwd=src)
        build=src/'wscript_build.py'; value=build.read_text()
        old='( "osdep/terminal-unix.c",               "posix" ),'
        if value.count(old)!=1: raise RuntimeError('mpv GUI terminal selection changed')
        build.write_text(value.replace(old,'( "osdep/terminal-dummy.c",              "posix" ), # Switchfin native GUI'))
        waf=fetch('waf',cache).read_bytes()
        (src/'waf').write_bytes(waf.replace(b'#!/usr/bin/env python\n',b'#!/usr/bin/env python3\n',1))
        (src/'waf').chmod(0o755); (src/'.native-patched').write_text('1\n')
    run(['python3','./waf','configure',f'--prefix={prefix}','--disable-libmpv-shared','--enable-libmpv-static','--disable-cplayer',
        '--enable-gl','--disable-iconv','--disable-jpeg','--disable-libavdevice','--disable-lua','--disable-javascript',
        '--disable-vapoursynth','--disable-manpage-build','--disable-html-build','--enable-sdl2','--enable-sdl2-audio',
        '--disable-sdl2-gamepad','--disable-sdl2-video'],cwd=src,env=env,log=work/'mpv-configure.log')
    run(['python3','./waf','build',f'-j{jobs}'],cwd=src,env=env,log=work/'mpv-build.log')
    run(['python3','./waf','install'],cwd=src,env=env,log=work/'mpv-install.log')
    shutil.copy2(src/'LICENSE.GPL',prefix/'share/licenses/mpv-GPL.txt')
    atomic_json(work/'mpv.json',{'archive_sha256':sha(prefix/'lib/libmpv.a'),'config_sha256':sha(src/'build/config.h'),
        'gui_source':'osdep/terminal-dummy.c','wscript_build_sha256':sha(src/'wscript_build.py'),
        'compatibility_patch_sha256':sha(HERE/'mpv.patch')})

def curl_native(prefix,work,cache,env,jobs,tools):
    src=extract('curl',cache,work)
    build=work/'curl-build'
    toolchain=work/'curl-toolchain.cmake'
    toolchain.write_text(f'''set(CMAKE_SYSTEM_NAME FreeBSD)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER "{tools/'cc'}")
set(CMAKE_AR llvm-ar-18)
set(CMAKE_RANLIB llvm-ranlib-18)
set(CMAKE_FIND_ROOT_PATH "{prefix}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
''')
    command=['cmake','-S',src,'-B',build,'-G','Ninja',f'-DCMAKE_TOOLCHAIN_FILE={toolchain}',
        f'-DCMAKE_INSTALL_PREFIX={prefix}','-DCMAKE_INSTALL_LIBDIR=lib','-DCMAKE_BUILD_TYPE=Release',
        '-DBUILD_SHARED_LIBS=OFF','-DBUILD_STATIC_LIBS=ON','-DBUILD_CURL_EXE=OFF','-DBUILD_TESTING=OFF',
        '-DBUILD_LIBCURL_DOCS=OFF','-DBUILD_MISC_DOCS=OFF','-DCURL_USE_OPENSSL=ON',
        '-DCURL_USE_LIBPSL=ON','-DCURL_ZLIB=ON','-DCURL_ZSTD=ON','-DCURL_BROTLI=OFF',
        '-DCURL_USE_LIBSSH2=OFF','-DCURL_USE_LIBSSH=OFF','-DUSE_LIBIDN2=OFF',
        '-DCURL_USE_GSSAPI=OFF','-DUSE_NGHTTP2=OFF','-DUSE_NGTCP2=OFF','-DUSE_QUICHE=OFF',
        '-DCURL_DISABLE_LDAP=ON','-DENABLE_THREADED_RESOLVER=ON',
        '-DCURL_CA_BUNDLE=/app0/ca-bundle.crt','-DCURL_CA_PATH=none',
        f'-DPKG_CONFIG_EXECUTABLE={tools/"pkg-config"}',f'-DOPENSSL_ROOT_DIR={prefix}',
        '-DOPENSSL_USE_STATIC_LIBS=TRUE']
    run(command,env=env,log=work/'curl-configure.log')
    config=(build/'lib/curl_config.h').read_text()
    for missing in ('HAVE_IF_NAMETOINDEX','HAVE_PIPE2','HAVE_GETPWUID_R','HAVE_GETPWUID'):
        if re.search(r'^#define '+missing+r' 1$',config,re.M):
            raise RuntimeError(f'curl selected absent native API: {missing}')
    run(['cmake','--build',build,'-j',jobs],env=env,log=work/'curl-build.log')
    run(['cmake','--install',build],env=env,log=work/'curl-install.log')
    shutil.copy2(src/'COPYING',prefix/'share/licenses/curl.txt')
    atomic_json(work/'curl.json',{'version':PINS['curl']['version'],'source_pin':PINS['curl'],
        'native_config_sha256':sha(build/'lib/curl_config.h'),'configure_argv':list(map(str,command)),
        'archive_sha256':sha(prefix/'lib/libcurl.a'),'pkgconfig_sha256':sha(prefix/'lib/pkgconfig/libcurl.pc'),
        'headers':inventory(prefix/'include/curl'),'replaces_original_archive':'libcurl.a',
        'native_missing_apis':['if_nametoindex','pipe2','getpwuid_r','getpwuid'],
        'fallbacks':'Upstream native link-probed pipe/socketpair, explicit netrc/HOME paths, numeric IPv6 scopes.'})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prefix', 'work', 'cache', 'runtime', 'sdl', 'graphics'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--payload-prefix', type=Path, help='Use an extracted pinned SDK instead of downloading it')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    prefix, work, cache, runtime, sdl, gl = [getattr(args, name).resolve() for name in
        ('prefix', 'work', 'cache', 'runtime', 'sdl', 'graphics')]
    if args.jobs < 1:
        parser.error('jobs must be positive')
    if prefix.exists() or work.exists():
        parser.error('prefix and work must be new directories')
    def overlaps(a, b):
        return a == b or a.is_relative_to(b) or b.is_relative_to(a)
    inputs = [HERE, runtime, sdl, gl, cache]
    if args.payload_prefix:
        inputs.append(args.payload_prefix.resolve())
    if overlaps(prefix, work) or any(overlaps(destination, path) for destination in (prefix, work) for path in inputs):
        parser.error('output directories overlap an input or each other')
    work.mkdir(parents=True)
    cache.mkdir(parents=True, exist_ok=True)
    payload = args.payload_prefix.resolve() if args.payload_prefix else None
    if payload is None:
        archive = fetch('payload_sdk', cache)
        extracted = work / 'sdk'
        extracted.mkdir()
        with tarfile.open(archive) as stream:
            stream.extractall(extracted, filter='data')
        matches = list(extracted.glob('**/target/user/homebrew'))
        if len(matches) != 1:
            raise ValueError('Unexpected payload SDK layout')
        payload = matches[0]
    bootstrap(prefix, work, runtime, payload)
    ca = payload / 'etc/ca-bundle.crt'
    if sha(ca) != '11163db462ffae3918afdf46c70a8db70cfcbaa8077352290b62a9c03c36fe6c':
        raise ValueError('SDK certificate bundle changed')
    shutil.copy2(ca, prefix / 'share/native-media/ca-bundle.crt')
    shutil.copytree(payload / 'share/licenses', prefix / 'share/licenses/sdk')
    for notice in (payload / 'share/doc').rglob('*'):
        if notice.is_file() and notice.name.lower().startswith(('license', 'copying', 'copyright', 'notice')):
            destination = prefix / 'share/licenses/sdk-doc' / notice.relative_to(payload / 'share/doc')
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(notice, destination)
    tools = write_tools(prefix, work, runtime, sdl, gl)
    env = dict(os.environ, CC=str(tools / 'cc'), CXX=str(tools / 'cc'), AR='llvm-ar-18',
        RANLIB='llvm-ranlib-18', NM='llvm-nm-18', PKG_CONFIG=str(tools / 'pkg-config'),
        CFLAGS=f'-O2 -I{prefix}/include -I{gl}/include', CXXFLAGS=f'-O2 -I{prefix}/include -I{gl}/include',
        LDFLAGS=f'-L{prefix}/lib', TARGET='x86_64', SOURCE_DATE_EPOCH='1788912000')
    openssl(prefix, work, cache, env, args.jobs)
    fontconfig(prefix, work, cache, env, args.jobs, tools)
    mpv(prefix, work, cache, env, args.jobs)
    curl_native(prefix, work, cache, env, args.jobs, tools)
    for pc in (prefix / 'lib/pkgconfig').glob('*.pc'):
        text = pc.read_text()
        if '/opt/' in text and '/opt/' not in str(prefix):
            raise ValueError('Unresolved SDK path in ' + pc.name)
    atomic_json(prefix / 'media.json', {'sources': PINS, 'libraries': {
        path.name: sha(path) for path in sorted((prefix / 'lib').glob('*.a'))}})


if __name__ == '__main__':
    main()
