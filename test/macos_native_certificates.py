"""Real OpenSSH certificate handshakes with temporary CA/key/config on loopback."""
import argparse
import getpass
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app',type=Path)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    helpers=args.app.resolve()/'Contents/Helpers'
    report={'status':'running','checks':[]}
    with tempfile.TemporaryDirectory(prefix='kitty-ng-certificates-',dir='/tmp') as temporary:
        root=Path(temporary)
        (root/'keys').mkdir();(root/'Sessions').mkdir();(root/'SshHostCAs').mkdir()
        env=os.environ.copy();env['KITTY_NG_PORTABLE_DIR']=str(root);env.pop('KITTY_NG_GUI_PROMPTS',None)
        def run(command):
            return subprocess.run(list(map(str,command)),capture_output=True,check=True,env=env)
        for name in ('ca','host','user'):
            run(['/usr/bin/ssh-keygen','-q','-t','ed25519','-N','','-f',root/'keys'/name])
        user=getpass.getuser()
        run(['/usr/bin/ssh-keygen','-s',root/'keys/ca','-I','synthetic-host','-h','-n','127.0.0.1','-V','-1m:+1h',root/'keys/host.pub'])
        run(['/usr/bin/ssh-keygen','-s',root/'keys/ca','-I','synthetic-user','-n',user,'-V','-1m:+1h',root/'keys/user.pub'])
        empty=root/'empty';empty.write_text('\n')
        run([helpers/'kittygen',root/'keys/user','-O','private','-o',root/'keys/user.ppk','--new-passphrase',empty])
        with socket.socket() as reserve:reserve.bind(('127.0.0.1',0));port=reserve.getsockname()[1]
        config=root/'sshd.conf'
        config.write_text(f'Port {port}\nListenAddress 127.0.0.1\nHostKey {root}/keys/host\nHostCertificate {root}/keys/host-cert.pub\nPidFile {root}/sshd.pid\nTrustedUserCAKeys {root}/keys/ca.pub\nAuthorizedKeysFile none\nPasswordAuthentication no\nKbdInteractiveAuthentication no\nUsePAM no\nStrictModes yes\nAllowUsers {user}\nForceCommand /usr/bin/printf CERTIFICATE_OK\nLogLevel VERBOSE\n')
        public=(root/'keys/ca.pub').read_text().split()[1]
        ca=root/'SshHostCAs/synthetic'
        ca.write_text(f'PublicKey={public}\nValidity=127.0.0.1\n')
        profile=root/'Sessions/cert-test'
        profile.write_text(f'HostName\\127.0.0.1\\\nPortNumber\\{port}\\\nUserName\\{user}\\\nProtocol\\ssh\\\nPublicKeyFile\\keys/user.ppk\\\nDetachedCertificate\\keys/user-cert.pub\\\nAuthGSSAPI\\0\\\nAuthGSSAPIKEX\\0\\\n')
        run(['/usr/sbin/sshd','-t','-f',config])
        with (root/'sshd.log').open('wb') as log:
            daemon=subprocess.Popen(['/usr/sbin/sshd','-D','-e','-f',str(config)],stdout=log,stderr=log)
            try:
                for attempt in range(50):
                    if daemon.poll() is not None:raise RuntimeError((root/'sshd.log').read_text())
                    try:
                        with socket.create_connection(('127.0.0.1',port),timeout=.2):break
                    except OSError:time.sleep(.1)
                for arch in ('arm64','x86_64'):
                    command=['/usr/bin/arch','-'+arch,str(helpers/'klink'),'--portable-dir',str(root),'-load','cert-test','-batch','-v','synthetic-probe']
                    result=subprocess.run(command,input=b'',capture_output=True,env=env,cwd='/tmp',timeout=20)
                    assert result.returncode==0 and b'CERTIFICATE_OK' in result.stdout,(result.returncode,result.stderr.decode(errors='replace'),(root/'sshd.log').read_text())
                    assert b'cert' in result.stderr.lower(),result.stderr
                    report['checks'].append(arch+'-host-ca-and-relative-detached-user-certificate')
                    ca.write_text(f'PublicKey={public}\nValidity=example.invalid\n')
                    denied=subprocess.run(command,input=b'',capture_output=True,env=env,cwd='/tmp',timeout=20)
                    assert denied.returncode!=0 and b'CERTIFICATE_OK' not in denied.stdout
                    report['checks'].append(arch+'-host-ca-scope-mismatch-rejected')
                    ca.write_text(f'PublicKey={public}\nValidity=127.0.0.1\n')
                    print('PASS:',arch,'real host CA, detached user certificate, portable paths, wrong host scope rejection',flush=True)
                report['status']='passed'
            except BaseException as error:
                report.update(status='failed',error=repr(error))
                raise
            finally:
                daemon.terminate();daemon.wait(timeout=10)
                report['sshd_log']=(root/'sshd.log').read_text(errors='replace')
                report['binaries_sha256']={n:hashlib.sha256((helpers/n).read_bytes()).hexdigest() for n in ('klink','kittygen')}
                args.report.write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':main()
