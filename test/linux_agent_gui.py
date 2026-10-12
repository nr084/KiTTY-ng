"""Native Kageant add/list/remove-one GUI against a disposable private agent."""
import argparse,os,subprocess,tempfile,time,signal
from pathlib import Path
import pyatspi
p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);args=p.parse_args();root=pyatspi.Registry.getDesktop(0)
def nodes(n):
 yield n
 for i in range(n.childCount):
  try:yield from nodes(n.getChildAtIndex(i))
  except Exception:pass
def wait(fn):
 end=time.monotonic()+20
 while time.monotonic()<end:
  try:
   result=fn()
   if result:return result
  except Exception:pass
  time.sleep(.1)
 print('Synthetic agent GUI visible controls:',[(n.getRoleName(),n.name) for n in nodes(root) if n.getState().contains(pyatspi.STATE_SHOWING)],flush=True)
 print('Synthetic agent list:',output(),flush=True)
 raise AssertionError('Agent GUI timeout')
def find(name):return next((n for n in nodes(root) if n.name==name and n.getState().contains(pyatspi.STATE_SHOWING)),None)
def click(name,owner=None):
 root.clearCache();time.sleep(.15)
 n=wait(lambda:next((n for n in nodes(owner or root) if n.name==name and n.getState().contains(pyatspi.STATE_SHOWING)),None));r=n.queryComponent().getExtents(pyatspi.DESKTOP_COORDS);subprocess.run(['xdotool','mousemove','--sync',str(r.x+r.width//2),str(r.y+r.height//2),'click','1'],check=True)
def output():
 root.clearCache()
 values=[]
 for n in nodes(root):
  if n.getRoleName()=='text' and n.getState().contains(pyatspi.STATE_SHOWING):
   try:
    n.clearCache();values.append(n.queryText().getText(0,-1))
   except Exception:pass
 return '\n'.join(values)
with tempfile.TemporaryDirectory(prefix='kitty-ng-agent-gui-') as folder:
 portable=Path(folder);key=portable/'fixture.ppk';env={**os.environ,'KITTY_NG_PORTABLE_DIR':folder,'KITTY_NG_LANGUAGE':'en','G_DEBUG':'fatal-criticals'}
 subprocess.run([str(args.binary),'--tool','kittygen','-t','ed25519','-C','synthetic-agent-key','--new-passphrase','/dev/null','-o',str(key)],env=env,check=True,capture_output=True)
 child=subprocess.Popen([str(args.binary),'--agent'],env=env,start_new_session=True)
 try:
  click('Add key…');d=wait(lambda:next((n for n in nodes(root) if n.name=='Load key' and n.getRoleName() in ('file chooser','dialog','frame')),None));time.sleep(.3)
  subprocess.run(['xdotool','key','ctrl+l'],check=True);time.sleep(.2)
  entry=next(n for n in nodes(d) if n.getRoleName()=='text' and n.getState().contains(pyatspi.STATE_SHOWING) and n.queryComponent().getExtents(pyatspi.DESKTOP_COORDS).width>20);entry.queryEditableText().setTextContents(str(key));click('Open',d);time.sleep(.5)
  click('List keys');wait(lambda:'synthetic-agent-key' in output())
  click('Remove key…');d=wait(lambda:next((n for n in nodes(root) if n.name=='Remove key…' and n.getRoleName() in ('dialog','frame')),None));entry=next(n for n in nodes(d) if n.getRoleName()=='text');entry.queryEditableText().setTextContents('synthetic-agent-key');click('Remove',d)
  click('List keys');time.sleep(.3);assert 'synthetic-agent-key' not in output()
  print('PASS: native Kageant GUI adds, lists and removes one key by comment',flush=True)
 finally:
  os.killpg(child.pid,signal.SIGTERM);child.wait(timeout=10)
