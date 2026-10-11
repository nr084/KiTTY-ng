"""Real SCP/SFTP uploads, downloads, recursive Unicode paths and WM_DROPFILES.

Only synthetic root credentials and a loopback SSH server are used.
"""
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import socket
import subprocess
import sys
import tempfile
import threading
import paramiko

binary = Path(sys.argv[1]).resolve()
scenario = sys.argv[2] if len(sys.argv) > 2 else "all"
password = "synthetic-root-password"
vector = json.loads(Path(__file__).with_name("kittyng-password-vector.json").read_text())
key = paramiko.RSAKey.generate(2048)
fingerprint = "SHA256:" + base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")

with tempfile.TemporaryDirectory(prefix="file-transfer-test-", dir=binary.parent.parent) as temporary:
    root = Path(temporary)
    remote = root / "remote"
    home = "/home/root"
    cwd = "/current directory/текущий😀"
    for path in ["/incoming space/приём😀", home, cwd]:
        (remote / path.lstrip("/")).mkdir(parents=True, exist_ok=True)
    def resolve(path):
        if not path.startswith("/"): path = home + "/" + path
        result = (remote / path.lstrip("/")).resolve()
        assert result.is_relative_to(remote.resolve()), path
        return result
    class Handle(paramiko.SFTPHandle):
        def stat(self):
            file = self.readfile or self.writefile
            return paramiko.SFTPAttributes.from_stat(os.fstat(file.fileno()))
    class SFTP(paramiko.SFTPServerInterface):
        def canonicalize(self, path):
            return "/" + resolve(path).relative_to(remote).as_posix()
        def stat(self, path):
            try: return paramiko.SFTPAttributes.from_stat(resolve(path).stat())
            except OSError as error: return paramiko.SFTPServer.convert_errno(error.errno)
        lstat = stat
        def list_folder(self, path):
            try:
                result = []
                for child in resolve(path).iterdir():
                    attr = paramiko.SFTPAttributes.from_stat(child.stat()); attr.filename = child.name
                    result.append(attr)
                return result
            except OSError as error: return paramiko.SFTPServer.convert_errno(error.errno)
        def open(self, path, flags, attr):
            try:
                fd = os.open(resolve(path), flags | getattr(os, "O_BINARY", 0), 0o600)
                file = os.fdopen(fd, "r+b" if flags & os.O_RDWR else "wb" if flags & os.O_WRONLY else "rb")
                handle = Handle(flags)
                handle.readfile = handle.writefile = None
                if flags & (os.O_RDWR | os.O_WRONLY): handle.writefile = file
                if not flags & os.O_WRONLY: handle.readfile = file
                return handle
            except OSError as error: return paramiko.SFTPServer.convert_errno(error.errno)
        def mkdir(self, path, attr):
            try: resolve(path).mkdir(); return paramiko.SFTP_OK
            except OSError as error: return paramiko.SFTPServer.convert_errno(error.errno)
        def chattr(self, path, attr): return paramiko.SFTP_OK
    failures, authentications, queries, commands = [], [], [], []
    transports = []
    stop = threading.Event()
    interactive = False
    def line(channel):
        result = bytearray()
        while True:
            byte = channel.recv(1)
            if not byte: return bytes(result)
            result.extend(byte)
            if byte == b"\n": return bytes(result)
    def exact(channel, count):
        result = bytearray()
        while len(result) < count:
            part = channel.recv(count - len(result))
            if not part: raise AssertionError("premature SCP EOF")
            result.extend(part)
        return bytes(result)
    def scp(channel, command):
        try:
            words = shlex.split(command.decode("utf8")); commands.append(words)
            path = resolve(words[-1])
            if "-t" in words:
                stack = [path]; channel.sendall(b"\0")
                while True:
                    record = line(channel)
                    if not record: break
                    kind = record[:1]
                    if kind == b"E":
                        assert len(stack) > 1; stack.pop(); channel.sendall(b"\0"); continue
                    if kind == b"T": channel.sendall(b"\0"); continue
                    assert kind in (b"C", b"D"), record
                    _, count, name = record.rstrip(b"\n").split(b" ", 2)
                    name = name.decode("utf8")
                    assert name not in (".", "..") and "/" not in name and "\\" not in name
                    target = stack[-1] / name
                    if kind == b"D": target.mkdir(exist_ok=True); stack.append(target); channel.sendall(b"\0")
                    else:
                        channel.sendall(b"\0")
                        target.write_bytes(exact(channel, int(count)))
                        assert exact(channel, 1) == b"\0"; channel.sendall(b"\0")
            else:
                assert "-f" in words
                assert exact(channel, 1) == b"\0"
                payload = path.read_bytes()
                channel.sendall(b"C0644 " + str(len(payload)).encode() + b" " + path.name.encode("utf8") + b"\n")
                assert exact(channel, 1) == b"\0"
                channel.sendall(payload + b"\0")
                assert exact(channel, 1) == b"\0"
            channel.send_exit_status(0)
        except Exception as error:
            failures.append(repr(error) + " command=" + repr(command)); channel.send_exit_status(1)
        finally: channel.close()
    def shell(channel):
        try:
            buffer = b""
            while not stop.is_set() and channel.active:
                try: chunk = channel.recv(4096)
                except socket.timeout: continue
                if not chunk: break
                buffer += chunk
                while b"\r" in buffer:
                    command, buffer = buffer.split(b"\r", 1)
                    match = re.search(rb"(__kitty_ng_pwd_\d+:)", command)
                    if match:
                        queries.append(command)
                        channel.sendall(b"\x1b]2;" + match[1] + cwd.encode("utf8") + b"\x07")
        except Exception as error:
            if not stop.is_set(): failures.append(repr(error))
        finally: channel.close()
    class Server(paramiko.ServerInterface):
        def __init__(self): self.interactive = interactive
        def get_allowed_auths(self, username): return "keyboard-interactive" if self.interactive else "password"
        def check_auth_password(self, username, supplied):
            if username == "root" and supplied == password:
                authentications.append(username); return paramiko.AUTH_SUCCESSFUL
            return paramiko.AUTH_FAILED
        def check_auth_interactive(self, username, submethods):
            self.username = username
            return paramiko.InteractiveQuery("Transfer test", "Synthetic credentials", ("Password:", False))
        def check_auth_interactive_response(self, responses):
            return self.check_auth_password(self.username, responses[0] if len(responses) == 1 else "")
        def check_channel_request(self, kind, channel_id):
            return paramiko.OPEN_SUCCEEDED if kind == "session" else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED
        def check_channel_pty_request(self, *args): return True
        def check_channel_shell_request(self, channel):
            channel.settimeout(1)
            threading.Thread(target=shell, args=(channel,), daemon=True).start(); return True
        def check_channel_exec_request(self, channel, command):
            if not command.startswith(b"scp "): return False
            threading.Thread(target=scp, args=(channel, command), daemon=True).start(); return True
    listener = socket.socket(); listener.bind(("127.0.0.1", 0)); listener.listen(8); listener.settimeout(0.2)
    port = listener.getsockname()[1]
    def accept():
        while not stop.is_set():
            try: connection, _ = listener.accept()
            except socket.timeout: continue
            except OSError: return
            def serve(connection):
                transport = paramiko.Transport(connection); transports.append(transport)
                transport.add_server_key(key)
                transport.set_subsystem_handler("sftp", paramiko.SFTPServer, SFTP)
                try:
                    transport.start_server(server=Server())
                    channels = []  # Transport keeps weak references to channels.
                    while transport.is_active() and not stop.is_set():
                        channel = transport.accept(0.2)
                        if channel is not None: channels.append(channel)
                except Exception as error:
                    if not stop.is_set(): failures.append(repr(error))
                finally: transport.close()
            threading.Thread(target=serve, args=(connection,), daemon=True).start()
    threading.Thread(target=accept, daemon=True).start()
    settings = root / "settings"; (settings / "Sessions").mkdir(parents=True)
    (settings / "kitty.ini").write_text("[KiTTY]\nsavemode=dir\ncryptsalt=0\npscpport=*\n")
    source = root / "Файлы 😀"
    source.mkdir()
    files = [source / "hello world.txt", source / "русский😀 & файл.txt"]
    payloads = [bytes(range(256)) * 129, b"binary\0\r\n" * 2100]
    for file, payload in zip(files, payloads): file.write_bytes(payload)
    folder = source / "Каталог 😀"; folder.mkdir()
    (folder / "вложенный.txt").write_bytes(b"recursive payload\0")
    startup = subprocess.STARTUPINFO(); startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW; startup.wShowWindow = 0
    def profile(protocol, auto=False, extra=""):
        (settings / "Sessions/transfer").write_text(
            f"HostName\\127.0.0.1\\\nPortNumber\\{port}\\\nUserName\\root\\\nProtocol\\ssh\\\n"
            f"Password\\{vector['encoded']}\\\nTermType\\xterm\\\nWinSCPProtocol\\{protocol}\\\n"
            "PSCPOptions\\-r -batch -v\\\nPSCPRemoteDir\\/incoming space/приём😀\\\n"
            f"SCPAutoPwd\\{int(auto)}\\\nLineCodePage\\UTF-8\\\nAuthGSSAPI\\0\\\nAuthGSSAPIKEX\\0\\\n"
            "ConnectionSharing\\0\\\nCloseOnExit\\2\\\n" + extra, encoding="utf8")
    def run(option, *paths):
        command = [str(binary), "--portable-dir", str(settings), "-load", "transfer", "-hostkey", fingerprint,
                   option, *map(str, paths)]
        result = subprocess.run(command, timeout=30, capture_output=True, startupinfo=startup)
        assert result.returncode == 0, (option, result.returncode, result.stdout, result.stderr, failures)
        assert not failures, failures
    try:
        for protocol in (1, 0):
            if scenario not in ("all", "sftp" if protocol else "scp"): continue
            profile(protocol)
            run("-test-upload", *files, folder)
            incoming = resolve("/incoming space/приём😀")
            for file, payload in zip(files, payloads): assert (incoming / file.name).read_bytes() == payload
            assert (incoming / folder.name / "вложенный.txt").read_bytes() == b"recursive payload\0"
            target = root / f"получено-{protocol}😀.txt"
            run("-test-download", "/incoming space/приём😀/" + files[1].name, target)
            assert target.read_bytes() == payloads[1]
        if scenario in ("all", "drop"):
            interactive = True
            profile(1, auto=True)
            run("-test-drop-files", files[1], folder)
            assert queries, "WM_DROPFILES did not query the remote cwd"
            assert (resolve(cwd) / files[1].name).read_bytes() == payloads[1]
            assert (resolve(cwd) / folder.name / "вложенный.txt").read_bytes() == b"recursive payload\0"
        assert authentications, authentications
        if scenario in ("all", "scp"): assert commands, "SCP protocol was not exercised"
        # Only the explicitly created legacy profile contains a password value,
        # and that value stays encoded; no transfer snapshot is written to disk.
        assert password.encode() not in (settings / "Sessions/transfer").read_bytes()
        assert sorted(p.name for p in (settings / "Sessions").iterdir()) == ["transfer"]
        print("PASS:", binary.name, scenario + ": Unicode files/folders, saved root password, no temporary profiles")
    finally:
        stop.set(); listener.close()
        for transport in transports: transport.close()
