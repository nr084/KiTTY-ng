"""Native encrypted PPK/OpenSSH interoperability using disposable keys only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app',type=Path)
    parser.add_argument('--arch',choices=['arm64','x86_64'],required=True)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    binary=args.app.resolve()/'Contents/Helpers/kittygen'
    digest=hashlib.sha256(binary.read_bytes()).hexdigest()
    if args.report.exists():
        previous=json.loads(args.report.read_text())
        if previous.get('status')=='passed' and previous.get('binary_sha256')==digest:
            print('REUSE: native key interoperability',args.arch)
            return
    report=dict(status='running',architecture=args.arch,binary_sha256=digest,checks=[])
    with tempfile.TemporaryDirectory(prefix='kitty-ng-keys-',dir='/tmp') as temporary:
        root=Path(temporary)
        environment=os.environ.copy()
        environment['KITTY_NG_PORTABLE_DIR']=str(root)
        environment.pop('KITTY_NG_GUI_PROMPTS',None)
        empty=root/'empty';empty.write_text('\n')
        phrase=root/'phrase';phrase.write_text('synthetic key passphrase\n')
        changed_phrase=root/'newphrase';changed_phrase.write_text('changed synthetic passphrase\n')
        def invoke(*arguments,success=True):
            process=subprocess.run(['/usr/bin/arch','-'+args.arch,str(binary),*map(str,arguments)],capture_output=True,env=environment,timeout=60)
            if (process.returncode==0)!=success:
                raise AssertionError((arguments,process.returncode,process.stderr.decode(errors='replace')))
            return process.stdout
        try:
            for algorithm,bits in [('ed25519',None),('ecdsa',256),('rsa',2048)]:
                key=root/(algorithm+'.ppk')
                options=['-t',algorithm,'-C','synthetic-native-key','-o',key,'--new-passphrase',phrase]
                if bits:options+=['-b',bits]
                invoke(*options)
                assert key.stat().st_mode&0o777==0o600
                public=invoke(key,'-L').split()[:2]
                openssh=root/(algorithm+'.openssh')
                invoke(key,'-O','private-openssh-new','-o',openssh,'--old-passphrase',phrase,'--new-passphrase',empty)
                assert openssh.stat().st_mode&0o777==0o600
                derived=subprocess.check_output(['/usr/bin/ssh-keygen','-y','-f',str(openssh)]).split()[:2]
                assert public==derived
                replaced=root/(algorithm+'-changed.ppk')
                invoke(key,'-P','-o',replaced,'--old-passphrase',phrase,'--new-passphrase',changed_phrase)
                assert invoke(replaced,'-L').split()[:2]==public
                invoke(replaced,'-O','private-openssh-new','-o',root/'wrong-key','--old-passphrase',phrase,'--new-passphrase',empty,success=False)
                invoke(replaced,'-O','private-openssh-new','-o',openssh,'--old-passphrase',changed_phrase,'--new-passphrase',empty)
                assert subprocess.check_output(['/usr/bin/ssh-keygen','-y','-f',str(openssh)]).split()[:2]==public
                report['checks'].append(algorithm+'-encrypted-ppk-openssh-change-passphrase-permissions-wrong-passphrase')
                print('PASS:',args.arch,algorithm,'encrypted PPK/OpenSSH interoperability, passphrase change, mode 600, wrong passphrase rejection',flush=True)
            report['status']='passed'
        except BaseException as error:
            report.update(status='failed',error=repr(error))
            raise
        finally:
            args.report.write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':main()
