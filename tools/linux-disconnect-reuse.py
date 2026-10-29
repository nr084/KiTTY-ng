"""Prove reuse of utility checks after the Linux disconnected-keypress fix."""
from pathlib import Path
import hashlib,json,subprocess
root=Path(__file__).resolve().parents[1]
baseline_path=root/'dist/linux/native-11/results/native-acceptance.json'
old=json.loads(baseline_path.read_text());oldbuild=json.loads((root/'dist/linux/native-11/baseline-build.json').read_text())
new=json.loads((root/'build/linux-windows/linux-cross-build.json').read_text())
package=json.loads((root/'dist/linux/linux-package.json').read_text())
assert old['status']=='passed' and old['source_commit']==oldbuild['source_commit']
assert old['version']==new['version']
for key in ('compiler','sysroot'):assert oldbuild[key]==new[key],key
changed=subprocess.check_output(['git','diff','--name-only',old['source_commit'],new['source_commit']],cwd=root,text=True).splitlines()
assert all(p=='unix/window.c' or p.startswith(('test/','tools/','windows/','docs/')) for p in changed),changed
before=subprocess.check_output(['git','show',old['source_commit']+':unix/window.c'],cwd=root).decode()
after=subprocess.check_output(['git','show',new['source_commit']+':unix/window.c'],cwd=root).decode()
original='    if (inst->exited)\n        gtk_widget_destroy(inst->window);'
replacement='#ifndef KITTY_NG_LINUX\n'+original+'\n#endif\n    /* KiTTY-ng keeps disconnected sessions available for explicit restart. */'
assert before.replace(original,replacement)==after
suites={'language':['linux_language.py'],'ssh-sftp-agent':['linux_ssh.py'],'keygen-gui':['linux_utilities_gui.py'],'agent-gui':['linux_agent_gui.py']}
for name,files in suites.items():
 assert name in old['tests']
 for file in files:assert old['inputs'][name][file]==hashlib.sha256((root/'test'/file).read_bytes()).hexdigest(),file
proof={'kind':'disconnected-terminal-keypress-only','source_commit':new['source_commit'],'binaries':package['binaries'],'baseline':old,'baseline_sha256':hashlib.sha256(baseline_path.read_bytes()).hexdigest(),'changed_sources':changed,'compiler':new['compiler'],'suites':suites}
(root/'dist/linux-reuse.json').write_text(json.dumps(proof,indent=2))
print('PASS: four unaffected utility/SSH checks retain their original evidence; client GUI checks must run')
