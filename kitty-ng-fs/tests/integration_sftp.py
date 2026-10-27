#!/usr/bin/env python3
"""Exercise the native C SFTP core against a local OpenSSH sftp-server.

Requires a POSIX host, Python 3 and a native shared library built from src/sftp.c.
Only subprocess stdin/stdout pipes are used; no SSH connection or credentials.
Every modified file lives in a newly-created temporary test directory.
"""
import argparse
import ctypes.util
import shutil
import ctypes as C
import os
from pathlib import Path
import subprocess
import tempfile

CALLBACK_READ = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p, C.c_size_t)
CALLBACK_WRITE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p, C.c_size_t)
class IO(C.Structure):
    _fields_ = [('ctx', C.c_void_p), ('read', CALLBACK_READ), ('write', CALLBACK_WRITE)]
class Client(C.Structure):
    _fields_ = [('io', IO), ('next_id', C.c_uint32), ('last_status', C.c_uint32),
                ('version', C.c_int), ('alive', C.c_int), ('posix_rename', C.c_int),
                ('error', C.c_char * 512)]
class Handle(C.Structure):
    _fields_ = [('length', C.c_uint32), ('data', C.c_ubyte * 256)]
class Attrs(C.Structure):
    _fields_ = [('flags', C.c_uint32), ('size', C.c_uint64),
                ('uid', C.c_uint32), ('gid', C.c_uint32), ('permissions', C.c_uint32),
                ('atime', C.c_uint32), ('mtime', C.c_uint32)]
class Entry(C.Structure):
    _fields_ = [('name', C.c_char_p), ('attrs', Attrs)]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--library", required=True, type=Path,
                    help="Native shared library built from src/sftp.c")
parser.add_argument("--server", type=Path,
                    help="OpenSSH sftp-server executable (otherwise autodetected)")
args = parser.parse_args()
if os.name != "posix":
    parser.error("this integration fixture requires a POSIX host; Windows DLL tests are separate")
server_candidates = [args.server] if args.server else [
    Path('/usr/lib/openssh/sftp-server'), Path('/usr/lib/ssh/sftp-server'),
    Path('/usr/libexec/sftp-server'),
    Path(shutil.which('sftp-server') or '/nonexistent/sftp-server')]
server = next((p for p in server_candidates if p and p.is_file()), None)
if server is None:
    parser.error("OpenSSH sftp-server was not found; supply --server PATH")
if not args.library.is_file():
    parser.error("--library must name an existing native shared library")
lib = C.CDLL(str(args.library.resolve()))
lib.sf_init.argtypes = [C.POINTER(Client), IO]
lib.sf_realpath.argtypes = [C.POINTER(Client), C.c_char_p, C.POINTER(C.c_void_p)]
lib.sf_open.argtypes = [C.POINTER(Client), C.c_char_p, C.c_uint32, C.POINTER(Attrs), C.POINTER(Handle)]
lib.sf_read.argtypes = [C.POINTER(Client), C.POINTER(Handle), C.c_uint64, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
lib.sf_write.argtypes = [C.POINTER(Client), C.POINTER(Handle), C.c_uint64, C.c_void_p, C.c_size_t]
lib.sf_close.argtypes = [C.POINTER(Client), C.POINTER(Handle)]
lib.sf_stat.argtypes = [C.POINTER(Client), C.c_char_p, C.c_int, C.POINTER(Attrs)]
lib.sf_setstat.argtypes = [C.POINTER(Client), C.c_char_p, C.POINTER(Attrs)]
lib.sf_mkdir.argtypes = [C.POINTER(Client), C.c_char_p, C.POINTER(Attrs)]
lib.sf_rmdir.argtypes = lib.sf_remove.argtypes = [C.POINTER(Client), C.c_char_p]
lib.sf_rename.argtypes = [C.POINTER(Client), C.c_char_p, C.c_char_p, C.c_int]
lib.sf_opendir.argtypes = [C.POINTER(Client), C.c_char_p, C.POINTER(Handle)]
lib.sf_readdir.argtypes = [C.POINTER(Client), C.POINTER(Handle), C.POINTER(C.POINTER(Entry)), C.POINTER(C.c_size_t)]
lib.sf_entries_free.argtypes = [C.POINTER(Entry), C.c_size_t]
libc = C.CDLL(ctypes.util.find_library('c') or None)
libc.free.argtypes = [C.c_void_p]

with tempfile.TemporaryDirectory(prefix='kittyngfs-sftp-') as root:
    proc = subprocess.Popen([str(server)], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
    @CALLBACK_READ
    def read(ctx, buffer, size):
        try:
            result = bytearray()
            while len(result) < size:
                chunk = proc.stdout.read(size - len(result))
                if not chunk:
                    return 0
                result.extend(chunk)
            C.memmove(buffer, bytes(result), size)
            return 1
        except Exception:
            return 0
    @CALLBACK_WRITE
    def write(ctx, buffer, size):
        try:
            data = C.string_at(buffer, size)
            done = 0
            while done < size:
                n = proc.stdin.write(data[done:])
                if not n:
                    return 0
                done += n
            return 1
        except Exception:
            return 0
    c = Client()
    def ok(result):
        assert result == 0, (result, c.last_status, bytes(c.error))
    def b(path):
        return str(path).encode('utf-8')
    try:
        ok(lib.sf_init(C.byref(c), IO(None, read, write)))
        assert c.version == 3 and c.posix_rename
        real = C.c_void_p()
        ok(lib.sf_realpath(C.byref(c), b(root + '/.'), C.byref(real)))
        assert C.string_at(real) == b(root)
        libc.free(real)

        directory = Path(root) / 'каталог с пробелами'
        ok(lib.sf_mkdir(C.byref(c), b(directory), None))
        path = directory / "файл 'a' $b & c.bin"
        h = Handle()
        payload = bytes(range(256)) * 8193 + b'final'
        ok(lib.sf_open(C.byref(c), b(path), 2 | 8 | 32, None, C.byref(h)))
        for offset in range(0, len(payload), 32768):
            chunk = payload[offset:offset + 32768]
            ok(lib.sf_write(C.byref(c), C.byref(h), offset, chunk, len(chunk)))
        ok(lib.sf_close(C.byref(c), C.byref(h)))
        assert path.read_bytes() == payload

        ok(lib.sf_open(C.byref(c), b(path), 1, None, C.byref(h)))
        contents = bytearray()
        buf = C.create_string_buffer(32768)
        while True:
            received = C.c_size_t()
            result = lib.sf_read(C.byref(c), C.byref(h), len(contents), buf, len(buf), C.byref(received))
            if result == 1:
                break
            ok(result)
            contents.extend(buf.raw[:received.value])
        ok(lib.sf_close(C.byref(c), C.byref(h)))
        assert contents == payload
        a = Attrs()
        ok(lib.sf_stat(C.byref(c), b(path), 1, C.byref(a)))
        assert a.size == len(payload)
        changed = Attrs(flags=4 | 8, permissions=0o600, atime=1700000011, mtime=2147483649)
        ok(lib.sf_setstat(C.byref(c), b(path), C.byref(changed)))
        assert (path.stat().st_mode & 0o777) == 0o600
        assert int(path.stat().st_mtime) == 2147483649

        link = directory / 'link'
        link.symlink_to(path.name)
        ok(lib.sf_stat(C.byref(c), b(link), 0, C.byref(a)))
        assert a.permissions & 0o170000 == 0o120000
        ok(lib.sf_stat(C.byref(c), b(link), 1, C.byref(a)))
        assert a.size == len(payload)
        ok(lib.sf_opendir(C.byref(c), b(directory), C.byref(h)))
        names = []
        while True:
            entries = C.POINTER(Entry)()
            count = C.c_size_t()
            result = lib.sf_readdir(C.byref(c), C.byref(h), C.byref(entries), C.byref(count))
            if result == 1:
                break
            ok(result)
            names.extend(entries[i].name.decode('utf-8') for i in range(count.value))
            lib.sf_entries_free(entries, count.value)
        ok(lib.sf_close(C.byref(c), C.byref(h)))
        assert path.name in names and 'link' in names
        ok(lib.sf_remove(C.byref(c), b(link)))

        destination = directory / 'destination'
        destination.write_bytes(b'preserve this')
        assert lib.sf_rename(C.byref(c), b(path), b(destination), 0) == -1
        assert destination.read_bytes() == b'preserve this' and path.exists()
        ok(lib.sf_rename(C.byref(c), b(path), b(destination), 1))
        assert destination.read_bytes() == payload and not path.exists()
        ok(lib.sf_remove(C.byref(c), b(destination)))

        sparse = directory / 'sparse'
        offset = 5 * 1024**3 + 7
        ok(lib.sf_open(C.byref(c), b(sparse), 2 | 8 | 32, None, C.byref(h)))
        ok(lib.sf_write(C.byref(c), C.byref(h), offset, b'SPARSE', 6))
        ok(lib.sf_close(C.byref(c), C.byref(h)))
        ok(lib.sf_stat(C.byref(c), b(sparse), 1, C.byref(a)))
        assert a.size == offset + 6
        ok(lib.sf_open(C.byref(c), b(sparse), 1, None, C.byref(h)))
        received = C.c_size_t()
        ok(lib.sf_read(C.byref(c), C.byref(h), offset, buf, len(buf), C.byref(received)))
        assert received.value == 6 and buf.raw[:6] == b'SPARSE'
        ok(lib.sf_close(C.byref(c), C.byref(h)))
        ok(lib.sf_remove(C.byref(c), b(sparse)))
        ok(lib.sf_rmdir(C.byref(c), b(directory)))
        assert lib.sf_stat(C.byref(c), b(directory), 1, C.byref(a)) == -1
        assert c.last_status == 2 and c.alive
        print('OpenSSH interoperability passed: full file lifecycle, 2 MiB binary/Unicode transfer,')
        print('symlink stat/lstat, timestamps beyond 2038, guarded/atomic rename, 5 GiB sparse offsets.')
    finally:
        proc.stdin.close()
        proc.wait(timeout=5)
        assert proc.returncode == 0, proc.stderr.read()
