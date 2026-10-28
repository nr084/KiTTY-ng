from pathlib import Path
import hashlib,json,subprocess,argparse
parser=argparse.ArgumentParser(description='Prove reuse of unchanged Linux SSH/tab regressions after utility presentation changes.');parser.add_argument('--baseline-root',type=Path,required=True);args=parser.parse_args()
root=Path(__file__).resolve().parents[1];shared=args.baseline_root
old=json.loads((shared/'dist/linux/native-acceptance.json').read_text());new=json.loads((root/'build/linux-windows/linux-cross-build.json').read_text());oldbuild=json.loads((shared/'build/linux-windows/linux-cross-build.json').read_text())
assert old['status']=='passed' and old['source_commit']==oldbuild['source_commit']
assert new['compiler']==oldbuild['compiler'] and new['sysroot']==oldbuild['sysroot']
changed=subprocess.check_output(['git','diff','--name-only',old['source_commit'],new['source_commit']],text=True).splitlines()
allowed={'AGENTS.md','README.md','README.en.md','linux/tools-gui.c','utils/buildinfo.c','quick-commands-config.h','locale/builtin.h','locale/ru.json'}
assert all(p in allowed or p.startswith(('docs/','windows/','tools/','test/')) for p in changed),changed
base=json.loads(subprocess.check_output(['git','show',old['source_commit']+':locale/ru.json'],encoding='utf8'));current=json.loads((root/'locale/ru.json').read_text(encoding='utf8'));assert all(current.get(k)==v for k,v in base.items())
suites={'tabs':['linux_tabs.py','duplicate_auth_fixture.py'],'tabs-key':['linux_tabs.py','duplicate_auth_fixture.py'],'duplicate-password-retry':['duplicate_password_retry.py','kittyng-password-vector.json'],'ssh-sftp-agent':['linux_ssh.py']}
for name,files in suites.items():
 assert name in old['tests']
 for f in files:assert old['inputs'][name][f]==hashlib.sha256((root/'test'/f).read_bytes()).hexdigest(),f
proof={'kind':'utility-presentation-only','source_commit':new['source_commit'],'binaries':json.loads((root/'dist/linux/linux-package.json').read_text())['binaries'],'baseline':old,'baseline_sha256':hashlib.sha256((shared/'dist/linux/native-acceptance.json').read_bytes()).hexdigest(),'changed_sources':changed,'compiler':new['compiler'],'suites':suites}
(root/'dist/linux-reuse.json').write_text(json.dumps(proof,indent=2))
print('PASS: four unchanged regression suites eligible; current package GUI tests remain mandatory')
