"""Native KiTTYgen/agent UI checks preserving key data across language changes."""
import argparse,json,os,subprocess,tempfile,time,signal
from pathlib import Path
import pyatspi
parser=argparse.ArgumentParser();parser.add_argument('--binary',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
root=pyatspi.Registry.getDesktop(0)
def nodes(n):
 yield n
 for i in range(n.childCount):
  try:yield from nodes(n.getChildAtIndex(i))
  except Exception:pass

def wait(fn,timeout=20):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  try:
   value=fn()
   if value:return value
  except Exception:pass
  time.sleep(.1)
 raise AssertionError('Native GUI timeout')
def find(name):return next((n for n in nodes(root) if n.name==name),None)
def click(name):
 node=wait(lambda:find(name));rect=node.queryComponent().getExtents(pyatspi.DESKTOP_COORDS);subprocess.run(['xdotool','mousemove','--sync',str(rect.x+rect.width//2),str(rect.y+rect.height//2),'click','1'],check=True)
def text(n):return n.queryText().getText(0,-1)
before={p for p in Path('/tmp').glob('kitty-ng-keygen-*') if p.stat().st_uid==os.getuid()}
with tempfile.TemporaryDirectory(prefix='kitty-ng-utilities-test-') as directory:
 portable=Path(directory);(portable/'values.ini').write_text('[values]\nUILanguage=0\nPreserveMe=unchanged\n')
 env={**os.environ,'KITTY_NG_PORTABLE_DIR':str(portable),'G_DEBUG':'fatal-criticals'};env.pop('KITTY_NG_LANGUAGE',None)
 log=open(args.output/'utilities.log','w');p=subprocess.Popen([str(args.binary),'--keygen'],env=env,stdout=log,stderr=log,start_new_session=True)
 try:
  wait(lambda:find('Key comment'));comment=find('Key comment');comment.queryEditableText().setTextContents('synthetic comment — тест')
  click('Generate…')
  def public():return next((text(n) for n in nodes(root) if n.getRoleName()=='text' and text(n).startswith('ssh-ed25519 ')),None)
  key=wait(public)
  assert 'synthetic comment — тест' in key
  algorithm=find('Key algorithm');bits=find('Key size');size=text(bits)
  combo=next(n for n in nodes(root) if n.getRoleName()=='combo box' and n.name=='English');combo.querySelection().selectChild(1)
  wait(lambda:find('Создать…'));assert public()==key and text(find('Key comment'))=='synthetic comment — тест' and text(find('Key size'))==size
  combo=next(n for n in nodes(root) if n.getRoleName()=='combo box' and n.name=='Русский');combo.querySelection().selectChild(0);wait(lambda:find('Generate…'))
  assert public()==key and 'PreserveMe=unchanged' in (portable/'values.ini').read_text()
  assert p.poll() is None
  print('PASS: KiTTYgen generation, Unicode comment and RU/EN switching without data loss',flush=True)
 finally:
  os.killpg(p.pid,signal.SIGTERM);p.wait(timeout=10)
 log.close()

wait(lambda:not {p for p in Path('/tmp').glob('kitty-ng-keygen-*') if p.stat().st_uid==os.getuid()}-before)
print('PASS: private working key removed on termination',flush=True)
