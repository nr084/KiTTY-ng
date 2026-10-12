"""Native GTK integration checks on an isolated Xvfb desktop; no personal data."""
import argparse,json,os,subprocess,tempfile,time,signal
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--binary',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
import gi
gi.require_version('Gdk','3.0');from gi.repository import Gdk
import pyatspi
root=pyatspi.Registry.getDesktop(0)
def nodes(node):
 yield node
 for i in range(node.childCount):
  try:yield from nodes(node.getChildAtIndex(i))
  except Exception:pass

def wait(predicate,timeout=15):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  try:
   value=predicate()
   if value:return value
  except Exception:pass
  time.sleep(.1)
 raise AssertionError('Timed out waiting for GUI state')

def find(name):return next((n for n in nodes(root) if n.name==name),None)
def screenshot(name):
 display=Gdk.Display.get_default();screen=display.get_default_screen();window=screen.get_root_window();image=Gdk.pixbuf_get_from_window(window,0,0,screen.get_width(),screen.get_height());image.savev(str(args.output/name),'png',[],[])
with tempfile.TemporaryDirectory(prefix='kitty-ng-linux-test-') as directory:
 portable=Path(directory);(portable/'Sessions/Work').mkdir(parents=True)
 for name,host in [('demo','example.invalid'),('Work/Server','example.invalid')]:
  (portable/'Sessions'/name).write_text('HostName\\'+host+'\\\nProtocol\\ssh\\\nPortNumber\\22\\\nUnknownLegacy\\retained\\\n')
 env={**os.environ,'KITTY_NG_LANGUAGE':'en','G_DEBUG':'fatal-criticals','KITTY_NG_PORTABLE_DIR':str(portable)}
 log=open(args.output/'gui.log','w');process=subprocess.Popen([str(args.binary),'--portable-dir',str(portable)],env=env,stdout=log,stderr=log,start_new_session=True)
 try:
  wait(lambda:find('KiTTY-ng'))
  screenshot('linux-session-en.png')
  tree=[{'name':n.name,'role':n.getRoleName()} for n in nodes(root)]
  (args.output/'accessibility.json').write_text(json.dumps(tree,ensure_ascii=False,indent=2))
  assert find('Saved sessions') and find('KiTTYgen') and find('Kageant')
  assert process.poll() is None
  def action(name,index=0):
   node=wait(lambda:find(name));rect=node.queryComponent().getExtents(pyatspi.DESKTOP_COORDS);subprocess.run(['xdotool','mousemove','--sync',str(rect.x+rect.width//2),str(rect.y+rect.height//2),'click','1'],check=True)
  entry=find('Search');entry.queryEditableText().setTextContents('DEMO')
  wait(lambda:not find('<Work>'))
  assert find('demo')
  entry.queryEditableText().setTextContents('')
  wait(lambda:find('<Work>'))
  foldernode=find('<Work>');interface=foldernode.queryAction()
  print('Folder actions:',[interface.getName(i) for i in range(interface.nActions)],flush=True)
  table=next(n for n in nodes(root) if n.getRoleName()=='table');table.queryTable().addRowSelection(0)
  action('Open')
  wait(lambda:find('Server'))
  assert find('..') and not find('demo')
  table=next(n for n in nodes(root) if n.getRoleName()=='table');table.queryTable().addRowSelection(0);action('Open');wait(lambda:find('demo'))
  combo=next(n for n in nodes(root) if n.getRoleName()=='combo box')
  combo.querySelection().selectChild(1);action('Apply')
  wait(lambda:find('Сохранённые сеансы'))
  assert find('Русский') and find('Быстрый поиск') is None
  assert 'UILanguage=1' in (portable/'values.ini').read_text()
  screenshot('linux-session-ru.png')
  print('PASS: native GTK browser, case-insensitive search, folder navigation and saved RU language',flush=True)
  # Exercise real modal dialogs, persisted files, and preservation of legacy fields.
  combo=next(n for n in nodes(root) if n.getRoleName()=='combo box');combo.querySelection().selectChild(0);action('Применить');wait(lambda:find('Saved sessions'))
  (portable/'SSH-Keys').mkdir();keyfile=portable/'SSH-Keys/synthetic.ppk';keyfile.write_text('synthetic test fixture')
  action('New');dialog=wait(lambda:find('Profile name'));entry=next(n for n in nodes(dialog) if n.getRoleName()=='text');entry.queryEditableText().setTextContents('created');action('Continue')
  dialog=wait(lambda:find('Profile settings'));fields={n.name:n for n in nodes(dialog) if n.getRoleName() in ['text','password text']}
  assert fields['Port'].queryText().getText(0,-1)=='22'
  fields['Host'].queryEditableText().setTextContents('example.invalid');fields['User name'].queryEditableText().setTextContents('test-user');fields['Private key'].queryEditableText().setTextContents(str(keyfile));action('Save')
  wait(lambda:(portable/'Sessions/created').exists())
  from urllib.parse import unquote
  raw=unquote((portable/'Sessions/created').read_text());assert 'SSH-Keys/synthetic.ppk' in raw and str(portable) not in raw
  table=next(n for n in nodes(root) if n.getRoleName()=='table');names=[table.queryTable().getAccessibleAt(i,0).name for i in range(table.queryTable().nRows)];table.queryTable().addRowSelection(names.index('demo'))
  original=(portable/'Sessions/demo').read_bytes();action('Duplicate');dialog=wait(lambda:find('Profile name'));entry=next(n for n in nodes(dialog) if n.getRoleName()=='text');entry.queryEditableText().setTextContents('demo-copy');action('Continue');wait(lambda:find('Profile settings'));action('Save')
  wait(lambda:(portable/'Sessions/demo-copy').exists())
  assert b'UnknownLegacy\\retained\\' in (portable/'Sessions/demo-copy').read_bytes()
  assert (portable/'Sessions/demo').read_bytes()==original
  table=next(n for n in nodes(root) if n.getRoleName()=='table');names=[table.queryTable().getAccessibleAt(i,0).name for i in range(table.queryTable().nRows)];table.queryTable().addRowSelection(names.index('demo'))
  action('Edit');wait(lambda:find('Profile settings'))
  changed=original+('ExternallyUpdated'+chr(92)+'true'+chr(92)+'\n').encode()
  (portable/'Sessions/demo').write_bytes(changed);action('Save')
  wait(lambda:find('Profile changed while editing. Reopen it before saving.'))
  notice=next(n for n in nodes(root) if n.getRoleName() in ('dialog','alert') and any(c.name=='Profile changed while editing. Reopen it before saving.' for c in nodes(n)))
  close=next(n for n in nodes(notice) if n.name=='Close');rect=close.queryComponent().getExtents(pyatspi.DESKTOP_COORDS);subprocess.run(['xdotool','mousemove','--sync',str(rect.x+rect.width//2),str(rect.y+rect.height//2),'click','1'],check=True)
  assert (portable/'Sessions/demo').read_bytes()==changed
  print('PASS: concurrent external profile update is preserved',flush=True)
  action('New');wait(lambda:find('Profile name'));action('Cancel');assert not find('Profile settings')
  print('PASS: create/cancel/duplicate profiles, SSH/22, relative key path and unknown-field/source preservation',flush=True)

 except Exception:
  screenshot('failure.png')
  print('GUI assertion failed; screenshot saved',flush=True)
  raise
 finally:
  os.killpg(process.pid,signal.SIGTERM);process.wait(timeout=10);log.close()
