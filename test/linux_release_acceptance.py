"""Verify the shipped Linux package on the native acceptance host."""
import hashlib,json,os,subprocess,sys,time
from pathlib import Path
root=Path(sys.argv[1]).resolve();version=json.loads((root/'linux-package.json').read_text())['version'];app=root/'KiTTY-ng.AppDir';out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
package=json.loads((root/'linux-package.json').read_text());build=json.loads((root/'linux-cross-build.json').read_text())
binaries={name:hashlib.sha256((app/'usr/bin'/name).read_bytes()).hexdigest() for name in package['binaries']}
assert binaries==package['binaries']
report={'status':'running','version':version,'source_commit':build['source_commit'],'binaries':binaries,'tests':[]}
path=out/'native-acceptance.json'
def run(name,args):
 with (out/(name+'.log')).open('w') as log:
  result=subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,timeout=240)
 if result.returncode:report['status']='failed';report['failed_suite']=name;path.write_text(json.dumps(report,indent=2));raise RuntimeError(name+' failed: '+(out/(name+'.log')).read_text()[-5000:])
 report['tests'].append(name);path.write_text(json.dumps(report,indent=2));print('PASS: '+name,flush=True)
def gui(script,*args):return ['xvfb-run','-a','-s','-screen 0 1600x1000x24','dbus-run-session','--','python3',str(root/script),*map(str,args)]
run('appimage-version',['xvfb-run','-a',str(root/f'kitty-ng-{version}-x86_64.AppImage'),'--appimage-extract-and-run','--version'])
run('native-gui',gui('linux_native.py','--binary',app/'AppRun','--output',out/'native'))
run('tabs',gui('linux_tabs.py',app/'AppRun',out/'tabs'))
run('tabs-key',gui('linux_tabs.py',app/'AppRun',out/'tabs-key','--key'))
run('duplicate-password-retry',gui('duplicate_password_retry.py',app/'AppRun','--report',out/'duplicate-password-retry.json'))
run('ssh-sftp-agent',['python3',str(root/'linux_ssh.py'),'--bin-dir',str(app/'usr/bin'),'--output',str(out/'ssh')])
run('keygen-gui',gui('linux_utilities_gui.py','--binary',app/'AppRun','--output',out/'keygen'))
run('agent-gui',gui('linux_agent_gui.py','--binary',app/'AppRun'))
run('regular-user-gui',['python3',str(root/'linux_regular_user.py'),'--binary',str(app/'AppRun'),'--output',str(out/'regular')])
report['status']='passed';path.write_text(json.dumps(report,indent=2));print('PASS: shipped Linux package acceptance',flush=True)
