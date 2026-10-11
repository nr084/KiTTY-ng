"""Real SSH authentication against a loopback server, with synthetic credentials.

Requires paramiko in the test environment; it is not a runtime dependency.
"""
import base64
import hashlib
import json
import socket
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

import paramiko

password = "synthetic-root-password"
encoded = json.loads(Path(__file__).with_name("kittyng-password-vector.json").read_text())["encoded"]
key = paramiko.RSAKey.generate(2048)
fingerprint = "SHA256:" + base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")

class Server(paramiko.ServerInterface):
    def __init__(self, interactive):
        self.interactive = interactive
        self.authenticated = False
        self.command = threading.Event()
    def get_allowed_auths(self, username):
        return "keyboard-interactive" if self.interactive else "password"
    def check_auth_password(self, username, supplied):
        self.authenticated = username == "root" and supplied == password
        return paramiko.AUTH_SUCCESSFUL if self.authenticated else paramiko.AUTH_FAILED
    def check_auth_interactive(self, username, submethods):
        self.username = username
        return paramiko.InteractiveQuery("Local test", "Synthetic credentials only", ("Password:", False))
    def check_auth_interactive_response(self, responses):
        return self.check_auth_password(self.username, responses[0] if len(responses) == 1 else "")
    def check_channel_request(self, kind, chanid):
        return paramiko.OPEN_SUCCEEDED if kind == "session" else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED
    def check_channel_pty_request(self, *args): return True
    def check_channel_exec_request(self, channel, command):
        self.command.set(); return True
    def check_channel_shell_request(self, channel):
        self.command.set(); return True

reconnect = "--reconnect" in sys.argv[1:]
for argument in [a for a in sys.argv[1:] if a != "--reconnect"]:
    binary = Path(argument).resolve()
    for interactive in [False, True]:
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0)); listener.listen(1); listener.settimeout(15)
        port = listener.getsockname()[1]
        server = Server(interactive)
        failures = []
        connections = []
        def serve():
            transport = None
            try:
                for attempt in range(2 if reconnect else 1):
                    server.command.clear()
                    client, _ = listener.accept()
                    transport = paramiko.Transport(client)
                    transport.add_server_key(key)
                    transport.start_server(server=server)
                    channel = transport.accept(15)
                    if channel is None or not server.command.wait(15):
                        raise RuntimeError("SSH channel or command missing")
                    connections.append(attempt)
                    if reconnect and attempt == 0:
                        # A lost established session must reconnect with the
                        # saved password. A normal second exit must terminate.
                        channel.sendall(b"SSH_SESSION_STARTED\r\n")
                        # A round trip confirms that the client has processed
                        # the established channel before the injected outage.
                        transport.global_request("keepalive@openssh.com", wait=True)
                        # Shutdown wakes the concurrent Windows recv before
                        # closing its socket; close alone can leave that read
                        # holding the underlying TCP handle temporarily.
                        client.shutdown(socket.SHUT_RDWR)
                        transport.close()
                        continue
                    channel.sendall(b"SSH_TEST_OK\r\n")
                    channel.send_exit_status(0)
                    channel.shutdown_write()
                    channel.close()
                    # Let the client acknowledge channel close before closing TCP.
                    transport.join(5)
            except Exception as exc: failures.append(str(exc))
            finally:
                if transport: transport.close()
                listener.close()
        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        with tempfile.TemporaryDirectory(prefix="kitty-ng-ssh-test-") as directory:
            root = Path(directory)
            (root / "Sessions").mkdir()
            (root / "Sessions/root-test").write_text(
                f"HostName\\127.0.0.1\\\nPortNumber\\{port}\\\nUserName\\root\\\n"
                f"Protocol\\ssh\\\nTermType\\xterm\\\nPassword\\{encoded}\\\n"
                "CloseOnExit\\2\\\nRemoteCommand\\synthetic-test\\\n"
                "KiTTYRUSReconnectDelay\\1\\\n"
                "AuthGSSAPI\\0\\\nAuthGSSAPIKEX\\0\\\n", encoding="ascii")
            args = [str(binary), "--portable-dir", directory, "-load", "root-test", "-hostkey", fingerprint]
            args += ["-sshlog", str(root / "ssh.log")]
            if binary.stem == "plink": args += ["-batch", "synthetic-test"]
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       startupinfo=startup)
            try:
                stdout, stderr = process.communicate(timeout=20)
                result = subprocess.CompletedProcess(args, process.returncode, stdout, stderr)
            except subprocess.TimeoutExpired:
                process.kill(); process.communicate()
                print("Timed out:", binary.name, "connections:", connections,
                      "authenticated:", server.authenticated, "server failures:", failures,
                      flush=True)
                if (root / "ssh.log").exists():
                    print((root / "ssh.log").read_text(errors="replace")[-5000:], flush=True)
                raise
            worker.join(3)
            assert result.returncode == 0, (binary.name, result.returncode, server.authenticated, failures,
                                          result.stdout.decode(errors="replace"), result.stderr.decode(errors="replace"))
            assert server.authenticated and not failures, (binary.name, failures)
            assert len(connections) == (2 if reconnect else 1)
            if binary.stem == "plink": assert b"SSH_TEST_OK" in result.stdout
        print(f"PASS: {binary.parent.name}/{binary.name}: saved root password via "
              f"{'keyboard-interactive' if interactive else 'password'}"
              f"{' and reconnect' if reconnect else ' and normal exit'}", flush=True)
