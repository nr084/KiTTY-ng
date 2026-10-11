"""Real authenticated shell: startup scripts, shortcuts, input guard and UI state.

The native diagnostic captures OSC52 through a mock front-end callback; it never
reads or writes the user's desktop clipboard. Profiles and scripts are owned fixtures.
"""
import base64
import hashlib
import json
import os
from pathlib import Path
import socket
import select
import struct
import subprocess
import sys
import tempfile
import threading
import paramiko

binary = Path(sys.argv[1]).resolve()
encoded_password = json.loads(Path(__file__).with_name('kittyng-password-vector.json').read_text())['encoded']
server_key = paramiko.RSAKey.generate(2048)
fingerprint = 'SHA256:' + base64.b64encode(hashlib.sha256(server_key.asbytes()).digest()).decode().rstrip('=')

class Server(paramiko.ServerInterface):
    def __init__(self): self.ready = threading.Event()
    def get_allowed_auths(self, username): return 'password'
    def check_auth_password(self, username, password):
        return paramiko.AUTH_SUCCESSFUL if username == 'root' and password == 'synthetic-root-password' else paramiko.AUTH_FAILED
    def check_channel_request(self, kind, chanid): return paramiko.OPEN_SUCCEEDED if kind == 'session' else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED
    def check_channel_pty_request(self, *args): return True
    def check_channel_shell_request(self, channel): self.ready.set(); return True

with tempfile.TemporaryDirectory(prefix='kitty-ng-extensions-runtime-') as temporary:
    base = Path(temporary)
    original = base / 'original'; (original / 'scripts').mkdir(parents=True)
    (original / 'scripts/startup.sh').write_text("printf 'script literal \\n'\necho Привет😀\n", encoding='utf8')
    # Root relocation and unrelated CWD must not change the script reference.
    root = base / 'Перенесённый терминал 😀'; original.rename(root)
    unrelated = base / 'unrelated'; unrelated.mkdir()
    (root / 'Sessions').mkdir()
    (root / 'kitty.ini').write_text('[Shortcuts]\nlist={SHIFT}{F10}\n{SHIFT}{F10}=SHORTCUT_OK\\n\n', encoding='utf8')
    listener = socket.socket(); listener.bind(('127.0.0.1', 0)); listener.listen(1); listener.settimeout(15)
    port = listener.getsockname()[1]
    proxy_listener = socket.socket(); proxy_listener.bind(('127.0.0.1', 0)); proxy_listener.listen(1); proxy_listener.settimeout(15)
    proxy_port = proxy_listener.getsockname()[1]
    (root/'Proxies').mkdir()
    (root/'Proxies/corp').write_text(f'ProxyMethod\\2\\\nProxyHost\\127.0.0.1\\\nProxyPort\\{proxy_port}\\\nProxyLocalhost\\1\\\nProxyUsername\\proxy-user\\\nProxyPassword\\proxy-pass\\\n')
    failures = []; received = bytearray()
    proxy_used = threading.Event()
    def exact(connection, size):
        data = bytearray()
        while len(data) < size:
            part = connection.recv(size-len(data)); assert part
            data.extend(part)
        return bytes(data)
    def proxy_serve():
        client = remote = None
        try:
            client, _ = proxy_listener.accept(); client.settimeout(15)
            version, count = exact(client, 2); assert version == 5 and 2 in exact(client, count)
            client.sendall(b'\x05\x02')
            version, length = exact(client, 2); assert version == 1
            username = exact(client, length); length = exact(client, 1)[0]; password = exact(client, length)
            assert username == b'proxy-user' and password == b'proxy-pass'
            client.sendall(b'\x01\x00')
            version, command, reserved, kind = exact(client, 4); assert version == 5 and command == 1
            if kind == 1: host = socket.inet_ntoa(exact(client, 4))
            elif kind == 3: host = exact(client, exact(client, 1)[0]).decode('ascii')
            else: raise AssertionError('unexpected address family')
            destination = struct.unpack('!H', exact(client, 2))[0]
            assert host == '127.0.0.1' and destination == port
            remote = socket.create_connection((host, destination), timeout=10)
            client.sendall(b'\x05\x00\x00\x01\x7f\x00\x00\x01\x00\x00'); proxy_used.set()
            while True:
                readable, _, _ = select.select((client, remote), (), (), 15)
                assert readable
                for source in readable:
                    data = source.recv(65536)
                    if not data: return
                    (remote if source is client else client).sendall(data)
        except Exception as error: failures.append('proxy: '+repr(error))
        finally:
            if client: client.close()
            if remote: remote.close()
            proxy_listener.close()
    def serve():
        transport = None
        try:
            client, _ = listener.accept(); transport = paramiko.Transport(client)
            transport.add_server_key(server_key); server = Server(); transport.start_server(server=server)
            channel = transport.accept(10); assert channel and server.ready.wait(10)
            channel.settimeout(10)
            while b'UI_PROBE_OK\r' not in received:
                part = channel.recv(4096); assert part, received
                received.extend(part)
            channel.send_exit_status(0); channel.shutdown_write(); channel.close()
            # Wait for the client's normal SSH disconnect before closing TCP.
            # An immediate transport.close races the SOCKS relay and can turn
            # a successful channel exit into a connection-reset error.
            transport.join(10)
        except Exception as error: failures.append(repr(error))
        finally:
            if transport: transport.close()
            listener.close()
    def field(name, value):
        value = ''.join(f'%{b:02X}' if b <= 32 or b > 126 or chr(b) in '\\*?:/"<>|%' else chr(b)
                        for b in value.encode('utf8'))
        return f'{name}\\{value}\\\n'
    (root / 'Sessions/Folder').mkdir()
    profile = root / 'Sessions/Folder/Сеанс'
    profile.write_text(''.join(field(k,v) for k,v in {
        'HostName':'127.0.0.1', 'PortNumber':str(port), 'Protocol':'ssh', 'UserName':'root',
        'TermType':'xterm', 'Password':encoded_password, 'CloseOnExit':'2', 'WarnOnClose':'0',
        'AuthGSSAPI':'0', 'AuthGSSAPIKEX':'0', 'LineCodePage':'UTF-8', 'EnableShortcuts':'1',
        'SaveWindowPos':'1', 'Scriptfile':'scripts\\startup.sh', 'Autocommand':'echo automatic\\n',
        'ProxySelection':'corp', 'ProxyUsername':'wrong-session-user', 'ProxyPassword':'wrong-session-pass',
        'InitDelay':'50', 'CommandDelay':'5', 'BCDelay':'1', 'UnknownLegacyOption':'retain this',
    }.items()), encoding='ascii')
    before = profile.read_bytes()
    worker = threading.Thread(target=serve, daemon=True); worker.start()
    proxy_worker = threading.Thread(target=proxy_serve, daemon=True); proxy_worker.start()
    env = os.environ.copy(); env['KITTY_NG_PORTABLE_DIR'] = str(root)
    startup = subprocess.STARTUPINFO(); startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW; startup.wShowWindow = 0
    process = subprocess.Popen([str(binary), '-load', 'Folder/Сеанс', '-hostkey', fingerprint, '-test-extensions'],
                               env=env, cwd=unrelated, startupinfo=startup, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        stdout, stderr = process.communicate(timeout=25)
    except subprocess.TimeoutExpired:
        process.kill(); process.communicate();
        worker.join(1); proxy_worker.join(1)
        raise AssertionError(('timeout', failures, received, proxy_used.is_set()))
    worker.join(3)
    proxy_worker.join(3)
    assert process.returncode == 0 and not failures, (process.returncode, failures, stderr, received)
    assert proxy_used.is_set()
    expected = "printf 'script literal \\n'\recho Привет😀\recho automatic\rSHORTCUT_OK\rUI_PROBE_OK\r".encode('utf8')
    assert bytes(received) == expected, (received, expected)
    after = profile.read_bytes()
    assert b'TermXPos\\40\\' in after and b'TermYPos\\60\\' in after, after
    for entry in before.splitlines():
        if not entry.startswith((b'TermXPos\\', b'TermYPos\\', b'WindowState\\')):
            assert entry in after.splitlines(), entry
    blocked_listener = socket.socket(); blocked_listener.bind(('127.0.0.1',0)); blocked_listener.listen(1)
    try:
        blocked = root/'Sessions/blocked'
        blocked.write_text(''.join(field(k,v) for k,v in {
            'HostName':'127.0.0.1','PortNumber':str(blocked_listener.getsockname()[1]),
            'Protocol':'ssh','ProxySelection':'missing','UserName':'root',
        }.items()), encoding='ascii')
        arch = binary.stem.rsplit('-',1)[1]
        result = subprocess.run([str(binary.with_name(f'klink-{arch}.exe')), '--language','en','-batch','-load','blocked'],
                                env=env,cwd=unrelated,capture_output=True,timeout=10)
        assert result.returncode != 0 and b'proxy profile' in result.stderr.lower(), (result.returncode,result.stderr)
        assert not select.select((blocked_listener,),(),(),0)[0], 'missing proxy was bypassed'
    finally: blocked_listener.close()
print('PASS: authenticated Unicode script, startup command, custom shortcut, input protection, OSC52 dispatch, window state and relocation')
