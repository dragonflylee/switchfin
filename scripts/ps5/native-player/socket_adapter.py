#!/usr/bin/env python3
"""Bind the pinned curl socket object to the native socket API.

Only symbol references change; executable sections and relocations are checked
before linking the adapted object ahead of the unchanged media archive.
"""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess

ARCHIVE_SHA = '92a14e525205cb7952ceeeafc378eb4d1e8ee052d476d5bfbedf081f388934c2'
MEMBER = 'cf-socket.c.o'
MEMBER_SHA = 'f0eb2be0ac59edb7cf5a55beb2fa11c0608040f71c82ba2aa04c5815521e2c60'
SYMBOL = 'ps5_native_curl_socket_fcntl'
IO_SYMBOLS = {'recv': 'ps5_native_curl_socket_recv', 'send': 'ps5_native_curl_socket_send'}
FILES = ('original.o', 'socket-adapter.o', 'binding.json')


def record(path):
    data = Path(path).read_bytes()
    return {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def member(data):
    if not data.startswith(b'!<arch>\n'):
        raise ValueError('invalid ordinary static archive')
    offset = 8
    found = []
    while offset < len(data):
        header = data[offset:offset+60]
        if len(header) != 60 or header[58:] != b'`\n':
            raise ValueError('invalid archive member header')
        size = int(header[48:58])
        start = offset + 60
        if size < 0 or start + size > len(data):
            raise ValueError('truncated archive member')
        if header[:16].decode('ascii').rstrip(' /') == MEMBER:
            found.append(data[start:start+size])
        offset = start + size + (size & 1)
    if offset != len(data) or len(found) != 1:
        raise ValueError('missing or duplicate cf-socket member')
    return found[0]


def object_structure(data, redirected=False, io=False):
    # Exact pinned native ABI: ELF64 little-endian, x86-64, ET_REL, no segments.
    if len(data) < 64 or data[:7] != b'\x7fELF\x02\x01\x01':
        raise ValueError('unsupported socket object ELF')
    header = struct.unpack_from('<HHIQQQIHHHHHH', data, 16)
    kind, machine, version, entry, phoff, shoff, flags, ehsize, phsize, phnum, shsize, shnum, names_index = header
    if (kind, machine, version, entry, phoff, phnum, ehsize, shsize) != (1, 62, 1, 0, 0, 0, 64, 64):
        raise ValueError('socket object ABI changed')
    if not shnum or names_index >= shnum or shoff+shnum*64 > len(data):
        raise ValueError('invalid socket object sections')
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, shoff+i*64) for i in range(shnum)]
    def body(section):
        off, size = section[4:6]
        if off+size > len(data):
            raise ValueError('socket object section outside file')
        return data[off:off+size]
    def string(table, offset):
        if offset >= len(table) or b'\0' not in table[offset:]:
            raise ValueError('invalid socket object string')
        return table[offset:table.index(b'\0', offset)].decode('ascii')
    names = body(sections[names_index])
    by_name = {}
    section_names = [string(names, s[0]) for s in sections]
    for index, section in enumerate(sections):
        name = section_names[index]
        if name in by_name:
            raise ValueError('duplicate socket object section')
        by_name[name] = (index, section)
    if '.symtab' not in by_name:
        raise ValueError('missing socket object symbols')
    _, table = by_name['.symtab']
    if table[9] != 24 or table[5] % 24 or table[6] >= shnum:
        raise ValueError('invalid socket symbol table')
    strings = body(sections[table[6]])
    symbols = []
    bindings = {'fcntl': SYMBOL, **(IO_SYMBOLS if io else {})}
    selected = {new if redirected else old: old for old, new in bindings.items()}
    forbidden = set(bindings) if redirected else set(bindings.values())
    targets = {}
    for index, sym in enumerate(struct.iter_unpack('<IBBHQQ', body(table))):
        name, info, other, sec, value, size = sym
        name = string(strings, name)
        if name in selected:
            if sec != 0 or info >> 4 != 1:
                raise ValueError('socket flag target is not an undefined global')
            name = selected[name]
            targets[index] = name
        elif name in forbidden:
            raise ValueError('ambiguous socket flag target')
        symbols.append((name, info, other, section_names[sec] if sec < shnum else sec, value, size))
    if len(targets) != len(bindings) or set(targets.values()) != set(bindings):
        raise ValueError('missing or duplicate socket flag symbol')
    relocations = {}
    calls = []
    for name, (_, section) in by_name.items():
        if section[1] == 4:  # SHT_RELA
            if section[9] != 24 or section[5] % 24:
                raise ValueError('invalid socket relocations')
            entries = []
            for offset, info, addend in struct.iter_unpack('<QQq', body(section)):
                index = info >> 32
                if index >= len(symbols):
                    raise ValueError('invalid socket relocation symbol')
                entries.append((offset, info & 0xffffffff, symbols[index], addend))
                if index in targets:
                    calls.append((name, offset, info & 0xffffffff, addend, targets[index]))
            relocations[name] = entries
    expected = [('.rela.text.socket_open', 0xc2, 41, -4, 'fcntl'),
                ('.rela.text.cf_tcp_accept_connect', 0x22d, 41, -4, 'fcntl')]
    if io:
        expected += [('.rela.text.cf_socket_shutdown', 0xa7, 41, -4, 'recv'),
                     ('.rela.text.cf_socket_recv', 0x43, 41, -4, 'recv'),
                     ('.rela.text.cf_socket_send', 0x57, 41, -4, 'send')]
    if sorted(calls) != sorted(expected):
        raise ValueError('socket flag call sites changed')
    # Both original callers load F_SETFD=2 / FD_CLOEXEC=1 into ESI/EDX,
    # clear EAX for the original variadic declaration, then call through GOT.
    for name, offset, _, _, target in calls:
        code = body(by_name[name.removeprefix('.rela')][1])
        window = code[offset-24:offset+4]
        if code[offset-2:offset] != b'\xff\x15':
            raise ValueError('socket call instruction changed')
        if target == 'fcntl' and b'\xbe\x02\0\0\0\xba\x01\0\0\0\x31\xc0' not in window:
            raise ValueError('socket flag integer call ABI changed')
    allocated = {name: (s[1], s[2], s[3], s[5], s[8], b'' if s[1] == 8 else body(s))
                 for name, (_, s) in by_name.items() if s[2] & 2}
    return {'header': (kind,machine,version,entry,flags), 'allocated': allocated,
            'symbols': symbols, 'relocations': relocations, 'calls': calls}


def compare(original, adapted, io=False):
    if hashlib.sha256(original).hexdigest() != MEMBER_SHA:
        raise ValueError('pinned curl socket member changed')
    if object_structure(original, io=io) != object_structure(adapted, True, io):
        raise ValueError('socket adapter changed more than the selected symbol bindings')


def prepare(out, media):
    archive = Path(media)/'lib/libcurl.a'
    data = archive.read_bytes()
    if hashlib.sha256(data).hexdigest() != ARCHIVE_SHA:
        raise ValueError('socket adapter requires exact immutable curl archive')
    original = member(data)
    object_structure(original, io=True)
    if hashlib.sha256(original).hexdigest() != MEMBER_SHA:
        raise ValueError('pinned curl socket member changed')
    directory = Path(out)/'socket-adapter'
    if directory.exists():
        descriptor = {name:record(directory/name) for name in FILES}
        verify(out, descriptor)
        if json.loads((directory/'binding.json').read_text()).get('schema') != 2:
            raise ValueError('resumed build lacks the current socket I/O bindings')
        return directory/'socket-adapter.o', descriptor
    directory.mkdir()
    tool = Path(shutil.which('llvm-objcopy-18') or '')
    if not tool.is_file():
        raise ValueError('LLVM18 objcopy is required for isolated socket binding')
    tool = tool.resolve()
    (directory/'original.o').write_bytes(original)
    renames = [arg for old, new in {'fcntl': SYMBOL, **IO_SYMBOLS}.items()
               for arg in ('--redefine-sym', old + '=' + new)]
    subprocess.run([str(tool),*renames,
                    str(directory/'original.o'),str(directory/'socket-adapter.o')],check=True)
    compare(original,(directory/'socket-adapter.o').read_bytes(), io=True)
    binding = {'schema':2,'archive_sha256':ARCHIVE_SHA,'member':MEMBER,'member_sha256':MEMBER_SHA,
               'original':record(directory/'original.o'),'adapted':record(directory/'socket-adapter.o'),
               'tool':record(tool),'tool_name':'llvm-objcopy-18','symbol':SYMBOL,
               'relocations':5,'io_symbols':IO_SYMBOLS,'integer_abi_checked':True,'all_allocated_bytes_unchanged':True,
               'scope':'Only pinned cf-socket flag and recv/send references, including shutdown. Original archive unchanged. Link this object before media archives.'}
    (directory/'binding.json').write_text(json.dumps(binding,indent=2)+'\n')
    descriptor = {name:record(directory/name) for name in FILES}
    verify(out,descriptor)
    return directory/'socket-adapter.o', descriptor


def verify(out, descriptor):
    if set(descriptor) != set(FILES):
        raise ValueError('socket adapter evidence file set differs')
    directory = Path(out)/'socket-adapter'
    if directory.is_symlink():
        raise ValueError('redirected socket adapter directory')
    for name in FILES:
        path = directory/name
        if path.is_symlink() or record(path) != descriptor[name]:
            raise ValueError('socket adapter evidence identity differs')
    binding = json.loads((directory/'binding.json').read_text())
    schema = binding.get('schema')
    if (schema not in (1, 2) or binding.get('archive_sha256') != ARCHIVE_SHA
            or binding.get('member') != MEMBER or binding.get('member_sha256') != MEMBER_SHA
            or binding.get('symbol') != SYMBOL or binding.get('relocations') != (5 if schema == 2 else 2)
            or (schema == 2 and binding.get('io_symbols') != IO_SYMBOLS)
            or (schema == 1 and 'io_symbols' in binding)
            or binding.get('original') != descriptor['original.o']
            or binding.get('adapted') != descriptor['socket-adapter.o']):
        raise ValueError('socket adapter binding differs')
    compare((directory/'original.o').read_bytes(), (directory/'socket-adapter.o').read_bytes(), io=schema == 2)
