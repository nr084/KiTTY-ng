#!/usr/bin/env python3
"""Run the real WFX ABI tests against an isolated loopback SSH/SFTP server.
Requires Paramiko; never uses a user's profiles, host keys or network server.
"""
import argparse, base64, hashlib, hmac, json, os, posixpath, shutil, socket, stat, struct, textwrap
import subprocess, tempfile, threading, time
from types import SimpleNamespace
from pathlib import Path
import paramiko

def ppk(key, password):
    """Generate an isolated PPK v2 fixture, including encrypted MAC coverage."""
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    def string(b): return struct.pack('>I',len(b))+b
    def integer(n):
        b=n.to_bytes((n.bit_length()+7)//8,'big')
        if b[0]&128: b=b'\0'+b
        return string(b)
    numbers=key.key.private_numbers()
    private=b''.join(integer(n) for n in (numbers.d,numbers.p,numbers.q,numbers.iqmp))
    public=key.asbytes(); comment=b'isolated-test-key'
    enc=b'aes256-cbc' if password else b'none'
    if password: private+=b'\0'*((-len(private))%16)
    mac_data=b''.join(string(x) for x in (b'ssh-rsa',enc,comment,public,private))
    mac_key=hashlib.sha1(b'putty-private-key-file-mac-key'+password).digest()
    mac=hmac.new(mac_key,mac_data,hashlib.sha1).hexdigest()
    if password:
        aeskey=b''.join(hashlib.sha1(struct.pack('>I',i)+password).digest() for i in (0,1))[:32]
        encrypt=Cipher(algorithms.AES(aeskey),modes.CBC(b'\0'*16)).encryptor()
        private=encrypt.update(private)+encrypt.finalize()
    pub=textwrap.wrap(base64.b64encode(public).decode(),64)
    priv=textwrap.wrap(base64.b64encode(private).decode(),64)
    return '\n'.join(['PuTTY-User-Key-File-2: ssh-rsa','Encryption: '+enc.decode(),'Comment: '+comment.decode(),
        f'Public-Lines: {len(pub)}',*pub,f'Private-Lines: {len(priv)}',*priv,'Private-MAC: '+mac,''])

class Files(paramiko.SFTPServerInterface):
    lock = threading.RLock()
    nodes = {'/': [None, stat.S_IFDIR | 0o755, 1700000000]}
    @staticmethod
    def key(path): return posixpath.normpath('/' + path.lstrip('/'))
    @classmethod
    def attrs(cls, name):
        n = cls.nodes[name]; a = paramiko.SFTPAttributes()
        a.st_size = len(n[0]) if n[0] is not None else 0
        a.st_mode = n[1]; a.st_uid = a.st_gid = 1000
        a.st_atime = a.st_mtime = n[2]; return a
    def canonicalize(self, path): return self.key(path)
    def stat(self, path):
        path = self.key(path)
        with self.lock:
            return self.attrs(path) if path in self.nodes else paramiko.SFTP_NO_SUCH_FILE
    lstat = stat
    def list_folder(self, path):
        path = self.key(path)
        with self.lock:
            if path not in self.nodes: return paramiko.SFTP_NO_SUCH_FILE
            out = []
            for name in self.nodes:
                if name != path and posixpath.dirname(name) == path:
                    a = self.attrs(name); a.filename = posixpath.basename(name); out.append(a)
            return out
    def mkdir(self, path, attr):
        path = self.key(path)
        with self.lock:
            if path in self.nodes: return paramiko.SFTP_FAILURE
            self.nodes[path] = [None, stat.S_IFDIR | (attr.st_mode or 0o755), int(time.time())]
        return paramiko.SFTP_OK
    def remove(self, path):
        with self.lock:
            if self.nodes.pop(self.key(path), None) is None: return paramiko.SFTP_NO_SUCH_FILE
        return paramiko.SFTP_OK
    rmdir = remove
    def rename(self, src, dst):
        src, dst = self.key(src), self.key(dst)
        with self.lock:
            if src not in self.nodes: return paramiko.SFTP_NO_SUCH_FILE
            self.nodes[dst] = self.nodes.pop(src)
        return paramiko.SFTP_OK
    posix_rename = rename
    def chattr(self, path, attr):
        path = self.key(path)
        with self.lock:
            if path not in self.nodes: return paramiko.SFTP_NO_SUCH_FILE
            if attr.st_mtime is not None: self.nodes[path][2] = attr.st_mtime
            if attr.st_mode is not None: self.nodes[path][1] = attr.st_mode
        return paramiko.SFTP_OK
    def open(self, path, flags, attr):
        path = self.key(path)
        with self.lock:
            if path not in self.nodes:
                if not flags & os.O_CREAT: return paramiko.SFTP_NO_SUCH_FILE
                self.nodes[path] = [bytearray(), stat.S_IFREG | 0o644, int(time.time())]
            elif flags & os.O_EXCL: return paramiko.SFTP_FAILURE
            if flags & os.O_TRUNC: self.nodes[path][0] = bytearray()
            return FileHandle(path, flags)

class FileHandle(paramiko.SFTPHandle):
    def __init__(self, path, flags): super().__init__(flags); self.path = path
    def read(self, offset, length):
        with Files.lock: return bytes(Files.nodes[self.path][0][offset:offset+length])
    def write(self, offset, data):
        with Files.lock:
            buf = Files.nodes[self.path][0]
            if len(buf) < offset + len(data): buf.extend(b'\0' * (offset+len(data)-len(buf)))
            buf[offset:offset+len(data)] = data
        return paramiko.SFTP_OK
    def stat(self):
        with Files.lock: return Files.attrs(self.path)
    def chattr(self, attr): return Files(None).chattr(self.path, attr)

class SFTPServer(paramiko.SFTPServer):
    def _send_server_version(self):
        from paramiko.sftp import CMD_INIT, CMD_VERSION
        kind, data = self._read_packet()
        if kind != CMD_INIT: raise paramiko.SSHException('Expected SFTP init')
        message=paramiko.Message(); message.add_int(3)
        message.add_string('posix-rename@openssh.com'); message.add_string('1')
        self._send_packet(CMD_VERSION,message)
        return struct.unpack('>I',data[:4])[0]

class Server(paramiko.ServerInterface):
    public_key = None
    interactive = False
    stall = False
    def check_auth_interactive(self, username, submethods):
        return paramiko.InteractiveQuery('Test MFA','Synthetic authentication',('Password:',False),('OTP:',False))
    def check_auth_interactive_response(self, responses):
        return paramiko.AUTH_SUCCESSFUL if responses == ['test-only-password','test-only-password'] else paramiko.AUTH_FAILED
    def check_auth_publickey(self, username, key):
        return paramiko.AUTH_SUCCESSFUL if username == "fixture" and key.asbytes() == self.public_key else paramiko.AUTH_FAILED
    def check_auth_password(self, username, password):
        return paramiko.AUTH_SUCCESSFUL if not self.interactive and (username,password) == ('fixture','test-only-password') else paramiko.AUTH_FAILED
    def get_allowed_auths(self, username): return 'keyboard-interactive' if self.interactive else 'publickey,password'
    def check_channel_request(self, kind, chanid):
        return paramiko.OPEN_SUCCEEDED if kind == 'session' else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED


def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--dll',type=Path,required=True)
    ap.add_argument('--auth-harness',type=Path); ap.add_argument('--harness',type=Path,required=True); ap.add_argument('--report',type=Path,required=True)
    ap.add_argument("--extra-only",action="store_true")
    args=ap.parse_args(); hostkey=paramiko.RSAKey.generate(2048)
    listener=socket.socket(); listener.bind(('127.0.0.1',0)); listener.listen(16)
    port=listener.getsockname()[1]; stop=threading.Event(); clients=[]
    def serve(client):
        if Server.stall:
            stop.wait(10); client.close(); return
        t=paramiko.Transport(client); clients.append(t); t.add_server_key(hostkey)
        t.set_subsystem_handler('sftp',SFTPServer,Files)
        try:
            t.start_server(server=Server())
            while t.is_active() and not stop.wait(.05): pass
        except (EOFError,OSError,paramiko.SSHException): pass
        finally: t.close()
    def accept():
        while not stop.is_set():
            try: client,_=listener.accept()
            except OSError: break
            threading.Thread(target=serve,args=(client,),daemon=True).start()
    threading.Thread(target=accept,daemon=True).start()
    try:
        with tempfile.TemporaryDirectory(prefix='kitty-ng-fs-native-') as directory:
            root=Path(directory); dll=root/args.dll.name; shutil.copy2(args.dll,dll)
            (root/'Sessions').mkdir(); (root/'SshHostKeys').mkdir()
            profile={'Protocol':'ssh','HostName':'127.0.0.1','PortNumber':str(port),'UserName':'fixture','Password':'test-only-password','Agent':'0'}
            (root/'Sessions/TestServer').write_text(''.join(f'{k}\\{v}\\\n' for k,v in profile.items()),encoding='utf-8')
            (root/'kitty.ini').write_text('[KiTTY]\nsavemode=dir\ncryptsalt=2\n',encoding='ascii')
            key=f'0x{hostkey.public_numbers.e:x},0x{hostkey.public_numbers.n:x}'
            (root/'SshHostKeys'/f'rsa2@{port}:127.0.0.1'.replace(':','%3A')).write_text(key,encoding='ascii')
            # The portable backend's host-key filename escaping is the same as PuTTY registry keys.
            (root/'kittyngfs.ini').write_text('[KiTTYngFS]\nPortableDir='+str(root)+'\nRemoteRoot=/\nTimeoutSeconds=5\n',encoding='utf-16')
            result=SimpleNamespace(returncode=0)
            output='Additional cases; file-transfer tests are recorded separately.'
            if not args.extra_only:
                with args.report.with_suffix('.log').open('wb') as log:
                    result=subprocess.Popen([str(args.harness.resolve()),str(dll),str(root)],stdout=log,stderr=subprocess.STDOUT)
                    try: result.wait(timeout=30)
                    except subprocess.TimeoutExpired:
                        result.kill(); result.wait()
                output=args.report.with_suffix('.log').read_text(encoding='utf-8',errors='replace')
            cases = []
            if result.returncode == 0 and args.auth_harness:
                keyfile = root/'SshHostKeys'/f'rsa2@{port}%3A127.0.0.1'
                def auth_case(label, accept, connected, trusts, secrets, cancel=0, session=None):
                    run = subprocess.run([str(args.auth_harness.resolve()),str(dll),str(accept),str(connected),str(trusts),str(secrets),str(cancel)]+([session] if session else []),capture_output=True,timeout=25)
                    detail=run.stdout.decode('utf-8',errors='replace')+run.stderr.decode('utf-8',errors='replace')
                    cases.append({'case':label,'exit_code':run.returncode,'output':detail})
                    print(label, detail, flush=True)
                if not args.extra_only:
                    auth_case('cached key + saved password',0,1,0,0)
                    keyfile.unlink()
                    auth_case('unknown key rejected',0,0,1,0)
                    assert not keyfile.exists(), 'Rejected key must not be stored'
                    auth_case('unknown key explicitly accepted',1,1,1,0)
                    assert keyfile.read_text().strip() == key
                    keyfile.write_text('0x10001,0x1234',encoding='ascii')
                    auth_case('changed key rejected',0,0,1,0)
                    assert keyfile.read_text() == '0x10001,0x1234', 'Rejected changed key must be preserved'
                    keyfile.write_text(key,encoding='ascii')
                    profile.pop('Password')
                    (root/'Sessions/TestServer').write_text(''.join(f'{k}\\{v}\\\n' for k,v in profile.items()),encoding='utf-8')
                    auth_case('interactive password',0,1,0,1)
                    testkey=paramiko.RSAKey.generate(2048); Server.public_key=testkey.asbytes()
                    profile['PublicKeyFile']='fixture.ppk'
                    (root/'Sessions/TestServer').write_text(''.join(f'{k}\\{v}\\\n' for k,v in profile.items()),encoding='utf-8')
                    (root/'fixture.ppk').write_text(ppk(testkey,b''),encoding='ascii')
                    auth_case('relative PPK key without password',0,1,0,0)
                    (root/'fixture.ppk').write_text(ppk(testkey,b'fixture-passphrase'),encoding='ascii')
                    auth_case('relative encrypted PPK key + passphrase dialog',0,1,0,1)
                    auth_case('immediate cancellation',0,0,0,0,1)
                else:
                    def save():
                        (root/'Sessions/TestServer').write_text(''.join(f'{k}\\{v}\\\n' for k,v in profile.items()),encoding='utf-8')
                    profile.pop('Password',None); save()
                    Server.interactive=True
                    auth_case('keyboard-interactive with two masked prompts',0,1,0,2)
                    Server.interactive=False
                    profile['Password']='test-only-password'; save()
                    group=root/'Sessions/Группа';group.mkdir()
                    (group/'Test.Host').write_bytes((root/'Sessions/TestServer').read_bytes())
                    auth_case('Unicode group and original dotted profile identity',0,1,0,0,session='\\Группа\\Test·Host\\')
                    profile['ProxyMethod']='5'; profile['ProxyLocalhost']='1'; profile['ProxyTelnetCommand']='never-execute-this-program'; save()
                    auth_case('external proxy command rejected',0,0,0,0)
                    profile.pop('ProxyMethod'); profile.pop('ProxyLocalhost'); profile.pop('ProxyTelnetCommand');save()
                    Server.stall=True
                    auth_case('stalled SSH handshake times out',0,0,0,0)
                    Server.stall=False

            report={'exit_code':result.returncode,'output':output,'authentication':cases,'helper_executables_in_fixture':list(root.glob('*.exe'))}
            args.report.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            print(output); return result.returncode or int(any(c['exit_code'] for c in cases))
    finally:
        stop.set(); listener.close()
        for client in clients: client.close()
if __name__=='__main__': raise SystemExit(main())
