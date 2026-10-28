"""Native GUI fixture: capture literal SSH input, never execute a shell command."""
import argparse
import base64
import hashlib
import json
import logging
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time

import paramiko

logging.getLogger('paramiko').setLevel(logging.CRITICAL)
PASSWORD = 'Quick-Commands-Synthetic-10!'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path)
    parser.add_argument('--arch', choices=['arm64', 'x86_64'], required=True)
    parser.add_argument('--language', choices=['en', 'ru'], default='ru')
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    app = args.app.resolve()
    report = dict(status='running', architecture=args.arch,
                  main_sha256=hashlib.sha256((app/'Contents/MacOS/kitty-ng').read_bytes()).hexdigest(),
                  auth_events=[], commands=[], input_bytes={}, connections=[], failures=[])
    lock = threading.RLock()
    stopped = threading.Event()
    transports = []
    with tempfile.TemporaryDirectory(prefix='kitty-ng-quick10.', dir='/private/tmp') as temporary:
        root = Path(temporary)
        profiles = root/'portable'
        (profiles/'Sessions').mkdir(parents=True)
        original_ini = '[KiTTY]\nsavemode=dir\nSessionTabs=1\n[Other]\nvalue=Кириллица 😀\n'
        ini = profiles/'kitty.ini'
        ini.write_bytes(b'\xff\xfe'+original_ini.encode('utf-16-le'))
        listener = socket.socket()
        listener.bind(('127.0.0.1', 0))
        listener.listen()
        listener.settimeout(.2)
        port = listener.getsockname()[1]
        host = paramiko.RSAKey.generate(2048)
        hostkey = 'SHA256:'+base64.b64encode(hashlib.sha256(host.asbytes()).digest()).decode().rstrip('=')
        profile = f'HostName\\127.0.0.1\\\nPortNumber\\{port}\\\nProtocol\\ssh\\\nUserName\\root\\\nPublicKeyFile\\\\\nPassword\\\\\nAuthGSSAPI\\0\\\nAuthGSSAPIKEX\\0\\\nTryAgent\\0\\\nWarnOnClose\\1\\\nSaveOnExit\\0\\\nSaveWindowPos\\0\\\nLineCodePage\\UTF-8\\\nSSHManualHostKeys\\{hostkey}\\\n'
        (profiles/'Sessions/Quick-test').write_text(profile)
        before = {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (profiles/'Sessions').iterdir()}
        state = dict(root=str(profiles), port=port, launch=0, events=report)

        def update():
            with lock:
                pending = args.fixture.with_suffix('.tmp')
                pending.write_text(json.dumps(state, ensure_ascii=False, indent=2)+'\n')
                pending.replace(args.fixture)

        def event(key, value):
            with lock:
                report[key].append(value)
                update()

        class Server(paramiko.ServerInterface):
            def __init__(self, number):
                self.number = number

            def get_allowed_auths(self, user):
                return 'password'

            def check_auth_password(self, user, password):
                ok = user == 'root' and password == PASSWORD
                event('auth_events', dict(connection=self.number, accepted=ok))
                return paramiko.AUTH_SUCCESSFUL if ok else paramiko.AUTH_FAILED

            def check_channel_request(self, kind, channel):
                return paramiko.OPEN_SUCCEEDED if kind == 'session' else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

            def check_channel_pty_request(self, *args):
                return True

            def check_channel_window_change_request(self, *args):
                return True

            def check_channel_shell_request(self, channel):
                event('connections', self.number)

                def shell():
                    received = b''
                    line = b''
                    try:
                        channel.sendall(f'\x1b]0;Quick session {self.number}\x07Synthetic SSH / Тест быстрых команд\r\n$ '.encode())
                        channel.settimeout(.2)
                        while not stopped.is_set() and channel.active:
                            try:
                                chunk = channel.recv(65536)
                            except socket.timeout:
                                continue
                            if not chunk:
                                return
                            received += chunk
                            line += chunk
                            if PASSWORD.encode() in received:
                                event('failures', 'Password reached terminal input')
                                return
                            with lock:
                                report['input_bytes'][str(self.number)] = base64.b64encode(received).decode()
                                update()
                            while b'\r' in line:
                                command, line = line.split(b'\r', 1)
                                event('commands', dict(connection=self.number, text=command.decode('utf-8')))
                                channel.sendall(b'\r\nRECEIVED\r\n$ ')
                    except (OSError, EOFError, paramiko.SSHException):
                        pass
                threading.Thread(target=shell, daemon=True).start()
                return True

        def serve(sock, number):
            transport = paramiko.Transport(sock)
            transports.append(transport)
            transport.add_server_key(host)
            channels = []
            try:
                transport.start_server(server=Server(number))
                while transport.is_active() and not stopped.is_set():
                    channel = transport.accept(.2)
                    if channel:
                        channels.append(channel)
            except (OSError, EOFError, paramiko.SSHException):
                pass
            finally:
                transport.close()

        def accept():
            number = 0
            while not stopped.is_set():
                try:
                    sock, _ = listener.accept()
                except socket.timeout:
                    continue
                except OSError:
                    return
                number += 1
                threading.Thread(target=serve, args=(sock, number), daemon=True).start()

        threading.Thread(target=accept, daemon=True).start()
        env = os.environ.copy()
        env['KITTY_NG_PORTABLE_DIR'] = str(profiles)
        env.pop('SSH_AUTH_SOCK', None)
        env.pop('KITTY_NG_LANGUAGE', None)
        try:
            for launch in (1, 2, 3):
                snapshot = ini.read_bytes()
                with (root/f'process-{launch}.log').open('wb') as log:
                    process = subprocess.Popen(['/usr/bin/arch', '-'+args.arch, str(app/'Contents/MacOS/kitty-ng'), '--language', args.language], env=env, stdout=log, stderr=log)
                    state.update(launch=launch, pid=process.pid)
                    update()
                    if launch == 1:
                        deadline = time.monotonic()+15
                        while '[QuickCommands]' not in ini.read_text(encoding='utf-16'):
                            assert process.poll() is None and time.monotonic()<deadline
                            time.sleep(.05)
                        seeded = ini.read_text(encoding='utf-16')
                        assert 'apt -y update && apt -y upgrade && apt -y autoremove' in seeded
                        assert not report['connections']
                        report['example_created_without_execution'] = True
                    else:
                        time.sleep(.3)
                        assert ini.read_bytes() == snapshot, 'Restart rewrote the saved quick commands'
                        report['restart_preserved_commands'] = True
                        report.setdefault('restarts_verified', []).append(launch)
                    print('READY:', args.arch, 'launch', launch, flush=True)
                    process.wait(timeout=1800)
                    assert process.returncode == 0
            after = {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (profiles/'Sessions').iterdir()}
            assert before == after, 'Commands changed session profiles'
            text = ini.read_text(encoding='utf-16')
            assert '[Other]\nvalue=Кириллица 😀' in text and 'SessionTabs=1' in text
            assert ini.read_bytes().startswith(b'\xff\xfe')
            assert '[QuickCommands]' in text
            report.update(status='fixture-finished', profiles_before=before, profiles_after=after,
                          other_ini_sections_preserved=True, utf16_bom=True, ini_final=text)
            for p in root.rglob('*'):
                if p.is_file() and not p.is_socket():
                    assert PASSWORD.encode() not in p.read_bytes(), 'Runtime password persisted'
            report['no_secret_files'] = True
            assert not report['failures']
        except BaseException as error:
            report['status'] = 'failed'
            report['failures'].append(repr(error))
            raise
        finally:
            if 'process' in locals() and process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
            stopped.set()
            listener.close()
            for transport in transports:
                transport.close()
            args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')


if __name__ == '__main__':
    main()
