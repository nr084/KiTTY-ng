"""Disposable native GUI acceptance fixture for runtime-only duplicate auth.

Drive the real app via native UI, change only numeric controls in the fixture,
then quit. Reports contain counts and success flags, never credential values.
"""
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
from duplicate_auth_fixture import PPK, PUBLIC, PASSPHRASE

logging.getLogger('paramiko').setLevel(logging.CRITICAL)
INITIAL = 'Synthetic-Duplicate-Password-8!'
ROTATED = 'Synthetic-Rotated-Password-8!'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app',type=Path)
    parser.add_argument('--arch',choices=['arm64','x86_64'],required=True)
    parser.add_argument('--mode',choices=['password','key','agent','otp'],required=True)
    parser.add_argument('--language',choices=['ru','en'],default='ru')
    parser.add_argument('--unsaved',action='store_true',help='Prepare an empty login and use Quick connect without saving a profile')
    parser.add_argument('--fixture',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args();app=args.app.resolve();args.report.parent.mkdir(parents=True,exist_ok=True)
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    report=dict(status='running',architecture=args.arch,mode=args.mode,unsaved=args.unsaved,main_sha256=digest(app/'Contents/MacOS/kitty-ng'),auth_events=[],connections=[],commands=[],failures=[])
    lock=threading.RLock();stop=threading.Event();workers=[];transports=[];agent=None;process=None
    with tempfile.TemporaryDirectory(prefix='kitty-ng-duplicate8.',dir='/tmp') as temporary:
        root=Path(temporary);profiles=root/'profiles';(profiles/'Sessions').mkdir(parents=True);(profiles/'SSH-Keys').mkdir()
        host=paramiko.RSAKey.generate(2048);agent_key=paramiko.RSAKey.generate(2048)
        listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen();listener.settimeout(.2);port=listener.getsockname()[1]
        fingerprint='SHA256:'+base64.b64encode(hashlib.sha256(host.asbytes()).digest()).decode().rstrip('=')
        control=root/'control.json';control.write_text(json.dumps(dict(password_generation=0)))
        state=dict(root=str(profiles),control=str(control),port=port,hostkey=fingerprint,mode=args.mode,events=report)
        def update():
            with lock:
                temporary_file=args.fixture.with_suffix('.tmp');temporary_file.write_text(json.dumps(state,indent=2)+'\n');temporary_file.replace(args.fixture)
        def event(kind,entry):
            with lock:report[kind].append(entry);update()
        def spawn(fn,*a):
            t=threading.Thread(target=fn,args=a,daemon=True);workers.append(t);t.start()
        class Server(paramiko.ServerInterface):
            def __init__(self,connection):self.connection=connection
            def get_allowed_auths(self,username):return 'publickey' if args.mode in ('key','agent') else 'keyboard-interactive' if args.mode=='otp' else 'password'
            def check_auth_password(self,user,value):
                generation=json.loads(control.read_text())['password_generation'];ok=user=='root' and value==(ROTATED if generation else INITIAL)
                event('auth_events',dict(connection=self.connection,method='password',accepted=ok));return paramiko.AUTH_SUCCESSFUL if ok else paramiko.AUTH_FAILED
            def check_auth_publickey(self,user,key):
                valid=key.get_base64()==PUBLIC if args.mode=='key' else key.asbytes()==agent_key.asbytes();ok=user=='root' and valid
                event('auth_events',dict(connection=self.connection,method='publickey',accepted=ok));return paramiko.AUTH_SUCCESSFUL if ok else paramiko.AUTH_FAILED
            def check_auth_interactive(self,user,submethods):
                self.user=user;return paramiko.InteractiveQuery('Synthetic verification','One-time challenge',('Verification code:',False))
            def check_auth_interactive_response(self,responses):
                ok=self.user=='root' and responses==[str(123450+self.connection)]
                event('auth_events',dict(connection=self.connection,method='one-time-code',accepted=ok));return paramiko.AUTH_SUCCESSFUL if ok else paramiko.AUTH_FAILED
            def check_channel_request(self,kind,chanid):return paramiko.OPEN_SUCCEEDED if kind=='session' else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED
            def check_channel_pty_request(self,*a):return True
            def check_channel_window_change_request(self,*a):return True
            def check_channel_shell_request(self,channel):
                event('connections',self.connection)
                def shell():
                    try:
                        time.sleep(.1);channel.sendall(f'\x1b]0;Synthetic {args.mode} {self.connection}\x07Native duplicate test / Проверка дубликата\r\n$ '.encode());channel.settimeout(.2);buffer=b''
                        while not stop.is_set() and channel.active:
                            try:chunk=channel.recv(4096)
                            except socket.timeout:continue
                            if not chunk:return
                            buffer+=chunk
                            while b'\r' in buffer:
                                command,buffer=buffer.split(b'\r',1);value=command.decode('utf8',errors='replace')
                                if any(secret in value for secret in (INITIAL,ROTATED,PASSPHRASE)):
                                    report['failures'].append('secret reached terminal input');value='REDACTED'
                                event('commands',dict(connection=self.connection,text=value))
                                if value=='fixture-disconnect':
                                    channel.send_exit_status(1);channel.close();return
                                channel.sendall(b'\r\nOK\r\n$ ')
                    except (EOFError,OSError,paramiko.SSHException):pass
                spawn(shell);return True
        def serve(sock,number):
            transport=paramiko.Transport(sock);transports.append(transport);transport.add_server_key(host);channels=[]
            try:
                transport.start_server(server=Server(number))
                while transport.is_active() and not stop.is_set():
                    channel=transport.accept(.2)
                    if channel:channels.append(channel)
            except (EOFError,OSError,paramiko.SSHException):pass
            finally:transport.close()
        def accept():
            count=0
            while not stop.is_set():
                try:sock,_=listener.accept()
                except socket.timeout:continue
                except OSError:return
                count+=1;spawn(serve,sock,count)
        spawn(accept)
        keyfile='SSH-Keys/protected.ppk' if args.mode=='key' else ''
        if keyfile:(profiles/keyfile).write_bytes(PPK)
        login='' if args.unsaved else 'root'
        defaults=f'HostName\\127.0.0.1\\\nPortNumber\\{port}\\\nProtocol\\ssh\\\nUserName\\{login}\\\nPublicKeyFile\\{keyfile}\\\nPassword\\\\\nAuthGSSAPI\\0\\\nAuthGSSAPIKEX\\0\\\nTryAgent\\{int(args.mode=="agent")}\\\nSaveOnExit\\0\\\nSaveWindowPos\\0\\\nWarnOnClose\\1\\\nSSHManualHostKeys\\{fingerprint}\\\n'
        defaults+='CloseOnExit\\1\\\n'
        for name in ['Default%20Settings','Native-auth']:(profiles/'Sessions'/name).write_text(defaults)
        (profiles/'kitty.ini').write_text('[KiTTY]\nsavemode=dir\nSessionTabs=1\n')
        before={p.name:digest(p) for p in (profiles/'Sessions').iterdir()};report['profiles_before']=before
        env=os.environ.copy();env['KITTY_NG_PORTABLE_DIR']=str(profiles);env.pop('SSH_AUTH_SOCK',None);env.pop('KITTY_NG_LANGUAGE',None)
        try:
            with (root/'process.log').open('wb') as log:
                if args.mode=='agent':
                    key=profiles/'SSH-Keys/agent.openssh';agent_key.write_private_key_file(str(key));sock=root/'agent.sock'
                    empty=root/'empty';empty.write_text('\n');ppk=profiles/'SSH-Keys/agent.ppk'
                    subprocess.run(['/usr/bin/arch','-'+args.arch,str(app/'Contents/Helpers/kittygen'),str(key),'-O','private','-o',str(ppk),'--new-passphrase',str(empty)],check=True,capture_output=True,env=env,timeout=30)
                    key=ppk
                    agent=subprocess.Popen(['/usr/bin/arch','-'+args.arch,str(app/'Contents/Helpers/kageant'),'--foreground','--symlink',str(sock),str(key)],env=env,stdout=log,stderr=log)
                    deadline=time.monotonic()+10
                    while not sock.exists():
                        assert agent.poll() is None and time.monotonic()<deadline,'agent failed to start: '+(root/'process.log').read_text(errors='replace');time.sleep(.05)
                    env['SSH_AUTH_SOCK']=str(sock)
                command=['/usr/bin/arch','-'+args.arch,str(app/'Contents/MacOS/kitty-ng'),'--language',args.language]
                process=subprocess.Popen(command,env=env,stdout=log,stderr=log);state['pid']=process.pid;update();print('READY:',args.fixture,flush=True)
                process.wait(timeout=1800);report['exit_code']=process.returncode;assert process.returncode==0,'GUI abnormal exit'
            report['profiles_after']={p.name:digest(p) for p in (profiles/'Sessions').iterdir()};assert before==report['profiles_after'],'portable profiles changed'
            leaked=[]
            for p in root.rglob('*'):
                if p.is_file() and not p.is_socket():
                    data=p.read_bytes()
                    if any(secret.encode() in data for secret in (INITIAL,ROTATED,PASSPHRASE)):leaked.append(str(p.relative_to(root)))
            assert not leaked,'credential marker persisted in '+','.join(leaked)
            report['no_secret_files']=True;assert not report['failures'];report['status']='fixture-finished'
        except BaseException as error:
            report['status']='failed';report['failures'].append(type(error).__name__+': '+str(error));raise
        finally:
            if process and process.poll() is None:process.terminate();process.wait(timeout=10)
            if agent and agent.poll() is None:agent.terminate();agent.wait(timeout=10)
            stop.set();listener.close()
            for t in transports:t.close()
            args.report.write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':main()
