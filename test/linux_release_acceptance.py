"""Verify the shipped Linux package on the native acceptance host."""
import hashlib,json,os,subprocess,sys,time
from pathlib import Path
root=Path(sys.argv[1]).resolve();version=json.loads((root/'linux-package.json').read_text())['version'];app=root/'KiTTY-ng.AppDir';out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
package=json.loads((root/'linux-package.json').read_text());build=json.loads((root/'linux-cross-build.json').read_text())
binaries={name:hashlib.sha256((app/'usr/bin'/name).read_bytes()).hexdigest() for name in package['binaries']}
assert binaries==package['binaries']
report={'status':'running','version':version,'source_commit':build['source_commit'],'binaries':binaries,'tests':[]}
path=out/'native-acceptance.json'
previous=json.loads(path.read_text()) if '--resume' in sys.argv and path.exists() else {}
checkpoint_time=path.stat().st_mtime if path.exists() else 0
same_package=all(previous.get(k)==report[k] for k in ('version','source_commit','binaries'))
report['inputs']={}
def inputs(name):
 files=[p for p in root.glob('*.py') if p.name!='linux_release_acceptance.py' and (name=='utility-themes' or p.name!='linux_utility_theme.py')]
 if name=='duplicate-password-retry':files.append(root/'kittyng-password-vector.json')
 return files,{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in files}

reuse=json.loads((root/'linux-reuse.json').read_text()) if (root/'linux-reuse.json').exists() else {'suites':{}}
if reuse['suites']:assert reuse['source_commit']==report['source_commit'] and reuse['binaries']==binaries
report['reused_checks']={}
def run(name,args):
 files,digests=inputs(name)
 if name in reuse['suites']:
  baseline=reuse['baseline'];assert baseline['status']=='passed' and name in baseline['tests']
  for f in reuse['suites'][name]:assert baseline['inputs'][name][f]==digests[f]
  report['tests'].append(name);report['inputs'][name]=digests
  report['reused_checks'][name]={'source_commit':baseline['source_commit'],'binaries':baseline['binaries'],'report_sha256':reuse['baseline_sha256'],'reason':reuse['kind'],'changed_sources':reuse['changed_sources']}
  path.write_text(json.dumps(report,indent=2));print('REUSE unmodified regression: '+name,flush=True);return
 cached=previous.get('inputs',{}).get(name)
 # Older checkpoints predate input digests. Reuse only when every relevant
 # test file is still older than the checkpoint; otherwise run it again.
 unchanged=(all(cached.get(k)==v for k,v in digests.items())) if cached is not None else all(p.stat().st_mtime<=checkpoint_time for p in files)
 if same_package and name in previous.get('tests',[]) and unchanged:
  report['tests'].append(name);report['inputs'][name]=digests
  path.write_text(json.dumps(report,indent=2));print('REUSE: '+name,flush=True);return
 with (out/(name+'.log')).open('w') as log:
  result=subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,timeout=240)
 if result.returncode:report['status']='failed';report['failed_suite']=name;path.write_text(json.dumps(report,indent=2));raise RuntimeError(name+' failed: '+(out/(name+'.log')).read_text()[-5000:])
 report['tests'].append(name);report['inputs'][name]=digests;path.write_text(json.dumps(report,indent=2));print('PASS: '+name,flush=True)
def gui(script,*args):return ['xvfb-run','-a','-s','-screen 0 1600x1000x24 -ac -noreset','dbus-run-session','--','python3',str(root/script),*map(str,args)]
run('appimage-version',['xvfb-run','-a',str(root/f'kitty-ng-{version}-x86_64.AppImage'),'--appimage-extract-and-run','--version'])
run('native-gui',gui('linux_native.py','--binary',app/'AppRun','--output',out/'native'))
run('quick-commands',gui('linux_quick_commands.py',app/'AppRun'))
run('tabs',gui('linux_tabs.py',app/'AppRun',out/'tabs'))
run('tabs-key',gui('linux_tabs.py',app/'AppRun',out/'tabs-key','--key'))
run('duplicate-password-retry',gui('duplicate_password_retry.py',app/'AppRun','--report',out/'duplicate-password-retry.json'))
run('restart-password',gui('linux_restart_auth.py',app/'AppRun',out/'restart-password'))
run('restart-key',gui('linux_restart_auth.py',app/'AppRun',out/'restart-key','--key'))
run('language',['python3',str(root/'linux_language.py'),str(app/'usr/bin/klink')])
run('ssh-sftp-agent',['python3',str(root/'linux_ssh.py'),'--bin-dir',str(app/'usr/bin'),'--output',str(out/'ssh')])
run('keygen-gui',gui('linux_utilities_gui.py','--binary',app/'AppRun','--output',out/'keygen'))
run('agent-gui',gui('linux_agent_gui.py','--binary',app/'AppRun'))
run('regular-user-gui',['python3',str(root/'linux_regular_user.py'),'--binary',str(app/'AppRun'),'--output',str(out/'regular')])
run('utility-themes',gui('linux_utility_theme.py',app/'AppRun',out/'themes'))
report['status']='passed';path.write_text(json.dumps(report,indent=2));print('PASS: shipped Linux package acceptance',flush=True)
