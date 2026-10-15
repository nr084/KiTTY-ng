"""Native SSH/SFTP/SCP acceptance on loopback with disposable credentials only."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import shlex
import socket
import subprocess
import tempfile
import threading
import time

import paramiko

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app',type=Path)
    parser.add_argument('--arch',choices=['arm64','x86_64'],default='arm64')
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--gui-fixture',type=Path,help='Expose this disposable server for native UI acceptance until the app quits')
    parser.add_argument('--gui-scp',action='store_true')
    parser.add_argument('--gui-automation',action='store_true')
    parser.add_argument('--proxies-only',action='store_true',help='Run only new loopback HTTP/SOCKS5 proxy checks')
    args=parser.parse_args()
    helpers=args.app.resolve()/'Contents/Helpers'
    hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in helpers.iterdir() if p.is_file()}
    previous=json.loads(args.report.read_text()) if args.report.exists() else {}
    cached=previous.get('checks',[]) if previous.get('binaries_sha256')==hashes else []
    host_key=paramiko.RSAKey.generate(2048)
    client_key=paramiko.RSAKey.generate(2048)
    fingerprint='SHA256:'+base64.b64encode(hashlib.sha256(host_key.asbytes()).digest()).decode().rstrip('=')
    password='synthetic-root-password'
    vector=json.loads((ROOT/'test/kittyng-password-vector.json').read_text())
    report=dict(architecture=args.arch,status='running',checks=list(cached),failures=[])
    with tempfile.TemporaryDirectory(prefix='kitty-ng-smoke.',dir='/tmp') as temporary:
        root=Path(temporary)
        remote=root/'remote'
        remote.mkdir()
        settings=root/'profiles'
        (settings/'Sessions').mkdir(parents=True)
        (settings/'SSH-Keys').mkdir()
        client_key.write_private_key_file(str(settings/'SSH-Keys/client.openssh'))
        env=os.environ.copy()
        env['KITTY_NG_PORTABLE_DIR']=str(settings)
        env.pop('KITTY_NG_GUI_PROMPTS',None)
        env.pop('SSH_AUTH_SOCK',None)
        mode={'auth':'password'}
        gui_state={'connections':0,'commands':[],'outage_injected':False}
        def update_gui():
            if args.gui_fixture:
                current=json.loads(args.gui_fixture.read_text())
                current['events']=gui_state
                args.gui_fixture.write_text(json.dumps(current,indent=2)+'\n')
        stop=threading.Event()
        transports=[]
        workers=[]
        listener=socket.socket()
        listener.bind(('127.0.0.1',0))
        listener.listen(8)
        listener.settimeout(.2)
        port=listener.getsockname()[1]

        def resolve(path):
            destination=(remote/path.lstrip('/')).resolve()
            if not destination.is_relative_to(remote.resolve()):
                raise OSError('Escaping test root')
            return destination

        class SFTP(paramiko.SFTPServerInterface):
            def canonicalize(self,path): return '/'+path.lstrip('/')
            def stat(self,path):
                try:return paramiko.SFTPAttributes.from_stat(resolve(path).stat())
                except OSError as error:return paramiko.SFTPServer.convert_errno(error.errno)
            lstat=stat
            def list_folder(self,path):
                try:
                    result=[]
                    for child in resolve(path).iterdir():
                        attr=paramiko.SFTPAttributes.from_stat(child.stat())
                        attr.filename=child.name
                        result.append(attr)
                    return result
                except OSError as error:return paramiko.SFTPServer.convert_errno(error.errno)
            def open(self,path,flags,attr):
                try:
                    descriptor=os.open(resolve(path),flags,0o600)
                    file=os.fdopen(descriptor,'r+b' if flags & os.O_RDWR else 'wb' if flags & os.O_WRONLY else 'rb')
                    handle=paramiko.SFTPHandle(flags)
                    handle.readfile=handle.writefile=file
                    return handle
                except OSError as error:return paramiko.SFTPServer.convert_errno(error.errno)
            def mkdir(self,path,attr):
                try:resolve(path).mkdir();return paramiko.SFTP_OK
                except OSError as error:return paramiko.SFTPServer.convert_errno(error.errno)
            def remove(self,path):
                try:resolve(path).unlink();return paramiko.SFTP_OK
                except OSError as error:return paramiko.SFTPServer.convert_errno(error.errno)
            def chattr(self,path,attr):return paramiko.SFTP_OK

        def exact(channel,count):
            result=bytearray()
            while len(result)<count:
                part=channel.recv(count-len(result))
                if not part:raise AssertionError('Premature SCP EOF')
                result.extend(part)
            return bytes(result)

        def line(channel):
            result=bytearray()
            while True:
                byte=channel.recv(1)
                if not byte or byte==b'\n':return bytes(result)
                result.extend(byte)

        def scp(channel,command):
            try:
                words=shlex.split(command.decode())
                if words[0] == 'scp' and '--' in words:
                    words.remove('--')
                path=resolve(words[-1])
                if '-t' in words:
                    stack=[path]
                    channel.sendall(b'\0')
                    while record:=line(channel):
                        kind=record[:1]
                        if kind==b'E':stack.pop();channel.sendall(b'\0');continue
                        if kind==b'T':channel.sendall(b'\0');continue
                        assert kind in (b'C',b'D'),record
                        _,count,name=record.split(b' ',2)
                        name=name.decode()
                        assert name not in ('.','..') and '/' not in name
                        target=stack[-1]/name
                        if kind==b'D':target.mkdir(exist_ok=True);stack.append(target);channel.sendall(b'\0')
                        else:
                            channel.sendall(b'\0');target.write_bytes(exact(channel,int(count)))
                            assert exact(channel,1)==b'\0';channel.sendall(b'\0')
                else:
                    assert '-f' in words and exact(channel,1)==b'\0'
                    payload=path.read_bytes()
                    channel.sendall(b'C0644 '+str(len(payload)).encode()+b' '+path.name.encode()+b'\n')
                    assert exact(channel,1)==b'\0'
                    channel.sendall(payload+b'\0')
                    assert exact(channel,1)==b'\0'
                channel.send_exit_status(0)
            except Exception as error:
                report['failures'].append(repr(error)+' command='+repr(command))
                channel.send_exit_status(1)
            finally:channel.close()

        def reply(channel,command):
            try:
                # Exec success must reach the client before data and exit status.
                channel.settimeout(5)
                channel.recv(1)
                channel.sendall(b'NATIVE_SSH_OK\r\n'+command+b'\r\n')
                channel.send_exit_status(0)
                channel.shutdown_write()
                channel.close()
            except Exception as error:report['failures'].append(repr(error))

        def start(function,*arguments):
            thread=threading.Thread(target=function,args=arguments,daemon=True)
            workers.append(thread)
            thread.start()

        class Server(paramiko.ServerInterface):
            def get_allowed_auths(self,username):return mode['auth']
            def check_auth_password(self,username,supplied):
                return paramiko.AUTH_SUCCESSFUL if username=='root' and supplied==password else paramiko.AUTH_FAILED
            def check_auth_publickey(self,username,supplied):
                return paramiko.AUTH_SUCCESSFUL if username=='root' and supplied.asbytes()==client_key.asbytes() else paramiko.AUTH_FAILED
            def check_auth_interactive(self,username,submethods):
                self.username=username
                return paramiko.InteractiveQuery('Local test','Synthetic credentials',('Password:',False))
            def check_auth_interactive_response(self,responses):
                return self.check_auth_password(self.username,responses[0] if len(responses)==1 else '')
            def check_channel_request(self,kind,chanid):
                return paramiko.OPEN_SUCCEEDED if kind=='session' else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED
            def check_channel_pty_request(self,*arguments):return True
            def check_channel_shell_request(self,channel):
                gui_state['connections']+=1
                update_gui()
                def shell():
                    time.sleep(.1)
                    channel.sendall('NATIVE_GUI_READY\r\n┌──────────────────────┐\r\n│ Русский UTF-8 😀     │\r\n└──────────────────────┘\r\n$ '.encode())
                    channel.settimeout(.2)
                    buffer=b''
                    while not stop.is_set() and channel.active:
                        try:chunk=channel.recv(4096)
                        except socket.timeout:continue
                        if not chunk:break
                        buffer+=chunk
                        while b'\r' in buffer:
                            command,buffer=buffer.split(b'\r',1)
                            gui_state['commands'].append(command.decode('utf8',errors='replace'))
                            update_gui()
                            channel.sendall(b'\r\nECHO:'+command+b'\r\n$ ')
                            if args.gui_automation and command==b'AUTOCOMMAND_OK' and not gui_state['outage_injected']:
                                gui_state['outage_injected']=True
                                update_gui()
                                time.sleep(.2)
                                transport=channel.get_transport()
                                transport.sock.shutdown(socket.SHUT_RDWR)
                                transport.close()
                                return
                start(shell)
                return True
            def check_channel_exec_request(self,channel,command):
                start(scp if command.startswith(b'scp ') else reply,channel,command)
                return True

        def serve(connection):
            transport=paramiko.Transport(connection)
            transports.append(transport)
            transport.add_server_key(host_key)
            transport.set_subsystem_handler('sftp',paramiko.SFTPServer,SFTP)
            channels=[]
            try:
                transport.start_server(server=Server())
                while transport.is_active() and not stop.is_set():
                    channel=transport.accept(.2)
                    if channel:channels.append(channel)
            except (EOFError,ConnectionResetError):pass
            except Exception as error:
                if not stop.is_set():report['failures'].append(repr(error))
            finally:transport.close()

        def accept():
            while not stop.is_set():
                try:connection,_=listener.accept()
                except socket.timeout:continue
                except OSError:return
                start(serve,connection)

        start(accept)
        profile=settings/'Sessions/native-test'
        def save_profile(key=False):
            profile.write_text(f'HostName\\127.0.0.1\\\nPortNumber\\{port}\\\nUserName\\root\\\nProtocol\\ssh\\\n'
                f'Password\\{vector["encoded"] if not key else ""}\\\nPublicKeyFile\\{"SSH-Keys/client.ppk" if key else ""}\\\n'
                'AuthGSSAPI\\0\\\nAuthGSSAPIKEX\\0\\\nLineCodePage\\UTF-8\\\n',encoding='utf8')
        def invoke(name,*arguments,stdin=None,expected=0):
            command=['/usr/bin/arch','-'+args.arch,str(helpers/name),*map(str,arguments)]
            result=subprocess.run(command,input=stdin,capture_output=True,env=env,cwd='/tmp',timeout=30)
            if result.returncode != expected:
                raise AssertionError((name,result.returncode,result.stdout.decode(errors='replace'),result.stderr.decode(errors='replace')))
            return result
        def passed(name):
            report['checks'].append(name)
            print('PASS:',args.arch,name,flush=True)
        common=['--portable-dir',settings,'-load','native-test','-hostkey',fingerprint,'-batch']
        try:
            if args.proxies_only:
                from macos_native_proxies import check_proxies
                save_profile()
                check_proxies(profile, port, lambda: invoke('klink',*common,'proxy-probe',stdin=b''), passed)
                assert not report['failures'], report['failures']
                report['status']='passed'
                return
            if args.gui_fixture:
                save_profile()
                with profile.open('a') as stream:
                    destination="/приём с пробелами'$(literal)" if args.gui_scp else '/'
                    resolve(destination).mkdir(exist_ok=True)
                    stream.write(f'TermType\\xterm\\\nCloseOnExit\\2\\\nPSCPRemoteDir\\{destination}\\\nPSCPOptions\\-batch\\\nWinSCPProtocol\\{0 if args.gui_scp else 1}\\\n')
                (settings/'GUI данные😀.txt').write_text('Нативная передача из GUI\n',encoding='utf8')
                if args.gui_automation:
                    (settings/'scripts').mkdir()
                    (settings/'scripts/startup.txt').write_text('SCRIPT_OK\nСКРИПТ_UTF8_😀\n',encoding='utf8')
                    with profile.open('a') as stream:
                        stream.write('Autocommand\\AUTOCOMMAND_OK\\\nScriptfile\\scripts/startup.txt\\\nInitDelay\\0\\\nReconnectDelay\\1\\\nEnableShortcuts\\1\\\n')
                    (settings/'kitty.ini').write_text('[KiTTY]\nsavemode=dir\n[Shortcuts]\nprotect={CONTROL}{F9}\nlist={CONTROL}{SHIFT}{K}\n{CONTROL}{SHIFT}{K}=SHORTCUT_OK\\n\n')
                (settings/'Sessions/Папка').mkdir()
                (settings/'Sessions/Папка/вложенный').write_bytes(profile.read_bytes())
                gui_env=env.copy()
                gui_env['KITTY_NG_PORTABLE_DIR']=str(settings)
                args.gui_fixture.write_text(json.dumps(dict(root=str(settings),remote=str(remote),port=port,hostkey=fingerprint,app=str(args.app.resolve())),indent=2)+'\n')
                with (args.report.parent/'gui-process.log').open('wb') as log:
                    process=subprocess.Popen(['/usr/bin/arch','-'+args.arch,str(args.app.resolve()/'Contents/MacOS/kitty-ng')],env=gui_env,stdout=log,stderr=log)
                    print('READY: native UI fixture',args.gui_fixture,flush=True)
                    try:process.wait(timeout=1200)
                    except BaseException:
                        process.terminate();process.wait(timeout=10);raise
                report['status']='fixture-finished'
                report['events']=gui_state
                return
            save_profile()
            for authentication in ('password','keyboard-interactive'):
                if 'saved-password-'+authentication in cached:continue
                mode['auth']=authentication
                result=invoke('klink',*common,'native-probe',stdin=b'')
                assert b'NATIVE_SSH_OK' in result.stdout
                passed('saved-password-'+authentication)
            empty=root/'empty-passphrase'
            empty.write_text('\n')
            invoke('kittygen',settings/'SSH-Keys/client.openssh','-O','private','-o',settings/'SSH-Keys/client.ppk','--new-passphrase',empty)
            save_profile(key=True)
            mode['auth']='publickey'
            if 'relative-key-from-unrelated-cwd' not in cached:
                assert b'NATIVE_SSH_OK' in invoke('klink',*common,'key-probe',stdin=b'').stdout
                passed('relative-key-from-unrelated-cwd')
            data=root/'данные с пробелом😀.bin'
            payload=bytes(range(256))*4096
            data.write_bytes(payload)
            for protocol in ('sftp','scp'):
                if protocol+'-unicode-binary-upload-download' in cached:continue
                invoke('kscp',*common,'-'+protocol,data,'root@127.0.0.1:/')
                assert (remote/data.name).read_bytes()==payload
                destination=root/('download-'+protocol)
                source='/'+data.name
                if protocol=='scp':source=shlex.quote(source)
                invoke('kscp',*common,'-'+protocol,'root@127.0.0.1:'+source,destination)
                assert destination.read_bytes()==payload
                passed(protocol+'-unicode-binary-upload-download')
            tree=root/'каталог😀'
            (tree/'подкаталог').mkdir(parents=True)
            (tree/'подкаталог/файл.txt').write_text('Тест\n',encoding='utf8')
            invoke('kscp',*common,'-sftp','-r',tree,'root@127.0.0.1:/')
            assert (remote/tree.name/'подкаталог/файл.txt').read_bytes()==(tree/'подкаталог/файл.txt').read_bytes()
            passed('recursive-unicode-sftp')
            script=root/'sftp.commands'
            ksftp_file=root/'ksftp.txt'
            script.write_text(f'put "{data}" /ksftp.bin\nget /ksftp.bin "{ksftp_file}"\nquit\n')
            invoke('ksftp',*common,'-b',script)
            assert ksftp_file.read_bytes()==payload
            passed('ksftp-batch-upload-download')
            profile.write_text(profile.read_text().replace('PublicKeyFile\\SSH-Keys/client.ppk\\','PublicKeyFile\\\\'))
            result=invoke('kageant',settings/'SSH-Keys/client.ppk','--exec',helpers/'klink',*common,'agent-probe',stdin=b'')
            assert b'NATIVE_SSH_OK' in result.stdout
            passed('kageant-agent-public-key-authentication')
            # Wrong pinned host key must fail before any SSH command executes.
            wrong=common.copy();wrong[wrong.index(fingerprint)]='SHA256:'+base64.b64encode(b'\x00'*32).decode().rstrip('=')
            result=subprocess.run(['/usr/bin/arch','-'+args.arch,str(helpers/'klink'),*map(str,wrong),'must-not-execute'],input=b'',capture_output=True,env=env,timeout=15)
            assert result.returncode != 0 and b'NATIVE_SSH_OK' not in result.stdout
            passed('host-key-mismatch-rejected')
            assert not report['failures'],report['failures']
            report['status']='passed'
        except BaseException as error:
            report['status']='failed'
            report['error']=repr(error)
            raise
        finally:
            stop.set()
            listener.close()
            for transport in transports:transport.close()
            for worker in workers:worker.join(2)
            report['binaries_sha256']=hashes
            args.report.parent.mkdir(parents=True,exist_ok=True)
            args.report.write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':main()
