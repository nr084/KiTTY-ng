"""Verify conditional fullscreen-only fixture change and preserve other successful evidence."""
from pathlib import Path
import importlib.util,json,subprocess,hashlib
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('pipeline',root/'tools/local-build.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
path=root/'dist/local-tests.json';cache=json.loads(path.read_text())
extra="   if '--fullscreen' in sys.argv:\n    # A normal remote exit displays this informational modal before close_session.\n    notice=wait(lambda:next((h for h in enum() if pid(h)==plain.pid and cls(h)=='#32770' and any(title(c)=='Connection closed by remote host' for c in enum(h))),None))\n    u.PostMessageW(notice,0x111,1,0)\n    wait(lambda:not u.IsWindow(notice))\n"
current=(root/'test/windows_tabs.py').read_text().replace("    ok=next(c for c in enum(notice) if cls(c)=='Button' and title(c)=='OK')\n    u.SendMessageW(ok,0x00F5,0,0)  # BM_CLICK uses native MessageBox routing.",'    u.PostMessageW(notice,0x111,1,0)')
focus_extra="    if '--fullscreen' in sys.argv:\n     u.SetForegroundWindow(host);wait(lambda:u.GetForegroundWindow()==host)\n"
for key,entry in list(cache.items()):
 if '/session-tabs' not in key:continue
 # Aggregate x64 covered fullscreen too: keep only modes 0..2; mode 3 needs retest.
 if '/scenario-' in key and key.endswith('/scenario-3'):continue
 baseline=entry['source_commit']
 old=subprocess.check_output(['git','show',baseline+':test/windows_tabs.py'],cwd=root).decode()
 if old.replace("   channels[4].send_exit_status(0);channels[4].close()\n","   channels[4].send_exit_status(0);channels[4].close()\n"+extra).replace('   for repeat in range(2):\n','   for repeat in range(2):\n'+focus_extra)!=current:continue
 inputs=set(entry['inputs']);inputs.remove('test/windows_tabs.py')
 assert m.digest_files(inputs,baseline)==m.digest_files(inputs),'Production dependencies changed'
 fingerprint=m.digest_files(entry['inputs'])
 updated=dict(entry,fingerprint=fingerprint,reuse_reason='Only fullscreen conditional fixture handles expected remote-close modal; nonfullscreen code unchanged',original_fingerprint=entry['fingerprint'])
 if '/scenario-' not in key:
  if key.startswith('x86/'):continue # stale old aggregate never becomes current evidence
  for i in range(3):cache[key+'/scenario-'+str(i)]=dict(updated)
  del cache[key]
 else:cache[key]=updated
 print('Verified unaffected:',key)
backup=path.with_name('tests-before-fullscreen-fixture.json');backup.write_bytes(path.read_bytes())
path.write_text(json.dumps(cache,indent=2))
