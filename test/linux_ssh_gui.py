"""Exercise packaged GUI password authentication and duplicate-session Conf pipe."""
import argparse,os,pwd,secrets,signal,subprocess,tempfile,time,sys
from pathlib import Path
import pyatspi
parser=argparse.ArgumentParser();parser.add_argument('--binary',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--debug',action='store_true');parser.add_argument('--existing-account',action='store_true');args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
root=pyatspi.Registry.getDesktop(0)
def nodes(n):
 yield n
 for i in range(n.childCount):
  try:yield from nodes(n.getChildAtIndex(i))
  except Exception:pass
def wait(fn,timeout=25):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  try:
   value=fn()
   if value:return value
  except Exception:pass
  time.sleep(.1)
 raise AssertionError('Packaged SSH GUI timed out')
def find(name):return next((n for n in nodes(root) if n.name==name),None)
def function_key(name, control=False):
 from Xlib import X,XK,display
 from Xlib.ext import xtest
 d=display.Display();code=d.keysym_to_keycode(XK.string_to_keysym(name))
 modifier=d.keysym_to_keycode(XK.string_to_keysym("Control_L"))
 if control:xtest.fake_input(d,X.KeyPress,modifier)
 xtest.fake_input(d,X.KeyPress,code);xtest.fake_input(d,X.KeyRelease,code)
 if control:xtest.fake_input(d,X.KeyRelease,modifier)
 d.sync();d.close()

def click(name,owner=root):
 n=wait(lambda:next((n for n in nodes(owner) if n.name==name),None));rect=n.queryComponent().getExtents(pyatspi.DESKTOP_COORDS);subprocess.run(['xdotool','mousemove','--sync',str(rect.x+rect.width//2),str(rect.y+rect.height//2),'click','1'],check=True)
account='kittyng-test';user=pwd.getpwnam(account);home=Path(user.pw_dir)
marker=home/'gui-started';marker.unlink(missing_ok=True)
count=home/'gui-connections';shellpid=home/'gui-shell-pid'
count.unlink(missing_ok=True);shellpid.unlink(missing_ok=True)
remote_file=home/'GUI файл с пробелом.txt';remote_file.unlink(missing_ok=True)
password=sys.stdin.readline().rstrip('\n') if args.existing_account else secrets.token_urlsafe(24)
assert password
if not args.existing_account:subprocess.run(['chpasswd'],input=account+':'+password+'\n',text=True,check=True)
try:
 with tempfile.TemporaryDirectory(prefix='kitty-ng-ssh-gui-') as folder:
  portable=Path(folder);(portable/'Sessions').mkdir();(portable/'values.ini').write_text('[values]\nUILanguage=0\n')
  sep=chr(92)
  values={'HostName':'127.0.0.1','PortNumber':'22','Protocol':'ssh','EnableShortcuts':'1','UserName':account,'Autocommand':'touch '+str(marker)+'; echo connected >> '+str(count)+'; echo $$ > '+str(shellpid),'ReconnectDelay':'1'}
  (portable/'Sessions/Native').write_text(''.join(k+sep+v+sep+'\n' for k,v in values.items()))
  share=portable/'share';(share/'applications').mkdir(parents=True);config=portable/'config';config.mkdir()
  link_marker=portable/'opened-url';link_handler=portable/'link-handler.py';link_handler.write_text('import sys,os\nassert not os.environ.get("LD_LIBRARY_PATH")\nfrom pathlib import Path\nPath('+repr(str(link_marker))+').write_text(sys.argv[1])\n')
  (share/'applications/kittyng-url-test.desktop').write_text('[Desktop Entry]\nType=Application\nName=KiTTY-ng test URL handler\nExec=/usr/bin/python3 '+str(link_handler)+' %u\nMimeType=x-scheme-handler/https;\n')
  (config/'mimeapps.list').write_text('[Default Applications]\nx-scheme-handler/https=kittyng-url-test.desktop\n')
  env={**os.environ,'XDG_DATA_HOME':str(share),'XDG_CONFIG_HOME':str(config),'KITTY_NG_PORTABLE_DIR':str(portable),'G_DEBUG':'fatal-criticals'};env.pop('KITTY_NG_LANGUAGE',None)
  with (args.output/'ssh-gui.log').open('w') as log:
   process=subprocess.Popen([str(args.binary)],env=env,stdout=log,stderr=log,start_new_session=True)
   try:
    wait(lambda:find('Native'));table=next(n for n in nodes(root) if n.getRoleName()=='table');table.queryTable().addRowSelection(0)
    click('Edit');dialog=wait(lambda:find('Profile settings'));entry=next(n for n in nodes(dialog) if n.name=='Saved password');entry.queryEditableText().setTextContents(password);click('Save')
    assert password not in (portable/'Sessions/Native').read_text()
    click('Open');click('Accept');wait(marker.exists,35)
    windows=subprocess.check_output(['xdotool','search','--name','kittyng-test@'],text=True).split();terminal=windows[-1]
    if args.debug:
     pid=subprocess.check_output(['xdotool','getwindowpid',terminal],text=True).strip()
     trace=(args.output/'gdb.log').open('w')
     subprocess.Popen(['gdb','-batch','-p',pid,'-ex','continue','-ex','bt'],stdout=trace,stderr=trace)
     time.sleep(1)
    def menu():
     geometry=dict(line.split('=',1) for line in subprocess.check_output(['xdotool','getwindowgeometry','--shell',terminal],text=True).splitlines() if '=' in line)
     subprocess.run(['xdotool','windowactivate','--sync',terminal,'mousemove','--sync',str(int(geometry['X'])+100),str(int(geometry['Y'])+120),'click','3'],check=True)
    fixture=portable/'GUI файл с пробелом.txt';fixture.write_text('GUI private Conf pipe transfer — тест',encoding='utf-8')
    menu();click('Send files...');chooser=wait(lambda:next((n for n in nodes(root) if n.name=='Send files...' and n.getRoleName() in ('dialog','file chooser','frame')),None))
    wait(lambda:chooser.getState().contains(pyatspi.STATE_SHOWING));time.sleep(.3)
    chooser_id=subprocess.check_output(['xdotool','search','--onlyvisible','--name','^Send files'],text=True).split()[-1];subprocess.run(['xdotool','windowactivate','--sync',chooser_id],check=True)
    subprocess.run(['xdotool','key','ctrl+l'],check=True)
    def location():
     chooser.clearCache()
     for n in nodes(chooser):
      if n.getRoleName()=='text':
       try:
        n.queryEditableText();rect=n.queryComponent().getExtents(pyatspi.DESKTOP_COORDS)
        if rect.width>20:return n
       except Exception:pass
    field=wait(location);rect=field.queryComponent().getExtents(pyatspi.DESKTOP_COORDS);subprocess.run(['xdotool','mousemove','--sync',str(rect.x+10),str(rect.y+10),'click','1'],check=True);field.queryEditableText().setTextContents(str(portable)+'/');subprocess.run(['xdotool','key','Return'],check=True)
    wait(lambda:next((n for n in nodes(chooser) if n.name==fixture.name),None));click(fixture.name,chooser);click('Open',chooser)
    wait(remote_file.exists,35)
    assert remote_file.read_bytes()==fixture.read_bytes()
    progress=wait(lambda:find('KiTTY-ng — transfer completed'))
    click('Close',progress)
    print('PASS: GUI upload uses encrypted profile password and private Conf pipe',flush=True)
    menu();click('Receive file...');dialog=wait(lambda:find('Receive file…'))
    entry=next(n for n in nodes(dialog) if n.getRoleName()=='text');entry.queryEditableText().setTextContents(remote_file.name);click('Download',dialog)
    chooser=wait(lambda:next((n for n in nodes(root) if n.name=='Save in folder' and n.getRoleName() in ('dialog','file chooser','frame')),None))
    wait(lambda:chooser.getState().contains(pyatspi.STATE_SHOWING));time.sleep(.3)
    chooser_id=subprocess.check_output(['xdotool','search','--onlyvisible','--name','^Save in folder'],text=True).split()[-1];subprocess.run(['xdotool','windowactivate','--sync',chooser_id,'key','ctrl+l'],check=True)
    destination=portable/'received';destination.mkdir()
    field=wait(location);rect=field.queryComponent().getExtents(pyatspi.DESKTOP_COORDS);subprocess.run(['xdotool','mousemove','--sync',str(rect.x+10),str(rect.y+10),'click','1'],check=True);field.queryEditableText().setTextContents(str(destination)+'/');subprocess.run(['xdotool','key','Return'],check=True)
    time.sleep(.3)
    try:
     if chooser.getState().contains(pyatspi.STATE_SHOWING):click('Select',chooser)
    except Exception:pass
    received=destination/fixture.name;wait(received.exists,35);assert received.read_bytes()==fixture.read_bytes();progress=wait(lambda:find('KiTTY-ng — transfer completed'));click('Close',progress)
    print('PASS: GUI download preserves Unicode filename and file content',flush=True)
    dragfile=portable/'DND файл.txt';dragfile.write_text('native GTK drag fixture — тест');remote_drag=home/dragfile.name;remote_drag.unlink(missing_ok=True)
    drag_script=portable/'drag-source.py';drag_script.write_text("import gi,sys\ngi.require_version('Gtk','3.0')\nfrom gi.repository import Gtk,Gdk\nfrom pathlib import Path\nw=Gtk.Window(title='KiTTY-ng drag fixture')\nw.set_default_size(350,100)\nw.move(10,720)\nb=Gtk.Button(label='Drag fixture')\nw.add(b)\nb.drag_source_set(Gdk.ModifierType.BUTTON1_MASK,[Gtk.TargetEntry.new('text/uri-list',0,0)],Gdk.DragAction.COPY)\nb.connect('drag-data-get',lambda widget,context,selection,info,time:selection.set_uris([Path(sys.argv[1]).as_uri()]))\nw.show_all()\nGtk.main()\n")
    drag=subprocess.Popen(['python3',str(drag_script),str(dragfile)])
    try:
     def drag_geometry():
      result=subprocess.run(['xdotool','search','--onlyvisible','--name','^KiTTY-ng drag fixture'],capture_output=True,text=True)
      if result.returncode:return None
      return dict(line.split('=',1) for line in subprocess.check_output(['xdotool','getwindowgeometry','--shell',result.stdout.split()[-1]],text=True).splitlines() if '=' in line)
     area=wait(drag_geometry)
     from types import SimpleNamespace
     rect=SimpleNamespace(x=int(area['X']),y=int(area['Y']),width=int(area['WIDTH']),height=int(area['HEIGHT']))
     geometry=dict(line.split('=',1) for line in subprocess.check_output(['xdotool','getwindowgeometry','--shell',terminal],text=True).splitlines() if '=' in line)
     subprocess.run(['xdotool','mousemove','--sync',str(rect.x+rect.width//2),str(rect.y+rect.height//2),'mousedown','1','mousemove','--sync',str(rect.x+rect.width//2+20),str(rect.y+rect.height//2)],check=True);time.sleep(.2)
     subprocess.run(['xdotool','mousemove','--sync',str(int(geometry['X'])+150),str(int(geometry['Y'])+140)],check=True);time.sleep(.3);subprocess.run(['xdotool','mouseup','1'],check=True)
     wait(remote_drag.exists,35);assert remote_drag.read_bytes()==dragfile.read_bytes();progress=wait(lambda:find('KiTTY-ng — transfer completed'));click('Close',progress)
     print('PASS: physical GTK URI drag-and-drop uploads Unicode file to the home directory',flush=True)
    finally:
     drag.terminate();drag.wait(timeout=10);remote_drag.unlink(missing_ok=True)

    print('PASS: packaged GUI uses saved password, inherited Conf pipe and startup command',flush=True)
    (portable/'kitty.ini').write_text('[KiTTY]\nsavemode=dir\n[Shortcuts]\nprotect={CONTROL}{SHIFT}L\nlist={CONTROL}{SHIFT}Y\n{CONTROL}{SHIFT}Y=custom\n')
    command_marker=home/'gui-command';command_marker.unlink(missing_ok=True)
    menu();click('Run command…');dialog=wait(lambda:find('Run command…'));entry=next(n for n in nodes(dialog) if n.getRoleName()=='text');entry.queryEditableText().setTextContents('touch '+str(command_marker));click('Send',dialog);wait(command_marker.exists);command_marker.unlink()
    print('PASS: command popup sends through the established SSH session',flush=True)
    subprocess.run(['xdotool','windowmove','--sync',terminal,'100','100','windowsize','--sync',terminal,'800','450'],check=True);time.sleep(.5)
    subprocess.run(['xdotool','windowactivate','--sync',terminal,'type','--clearmodifiers',"printf '\\033[2J\\033[Hhttps://example.invalid/kitty-ng\\n'"],check=True);subprocess.run(['xdotool','key','Return'],check=True);time.sleep(.3)
    geometry=dict(line.split('=',1) for line in subprocess.check_output(['xdotool','getwindowgeometry','--shell',terminal],text=True).splitlines() if '=' in line)
    print('URL test client geometry:',geometry,flush=True)
    # Xfce may move the transient terminal after modal dialogs. Wait for its current position.
    time.sleep(.5)
    geometry=dict(line.split('=',1) for line in subprocess.check_output(['xdotool','getwindowgeometry','--shell',terminal],text=True).splitlines() if '=' in line)
    print('URL final geometry:',geometry,flush=True)
    for offset in (-40,-20,8,28,38,48):
     subprocess.run(['xdotool','keydown','Control_L','mousemove',str(int(geometry['X'])+30),str(int(geometry['Y'])+offset),'click','1','keyup','Control_L'],check=True)
     time.sleep(.3)
     if link_marker.exists():break
    wait(link_marker.exists);assert link_marker.read_text()=='https://example.invalid/kitty-ng'
    print('PASS: physical Ctrl-click opens the exact HTTPS URI using the isolated desktop handler',flush=True)
    capture=home/'gui-key-bytes';ready=home/'gui-key-ready';script=home/'gui-input-capture.py'
    for path in (capture,ready):path.unlink(missing_ok=True)
    script.write_text("import os,tty,termios,select,time\nfrom pathlib import Path\nold=termios.tcgetattr(0)\ntty.setraw(0)\nos.write(1,b'\\x1b[?1003h')\nPath("+repr(str(ready))+ ").touch()\ndata=b''\nend=time.monotonic()+8\ntry:\n while len(data)<21 and time.monotonic()<end:\n  if select.select([0],[],[],.2)[0]:data+=os.read(0,64)\nfinally:termios.tcsetattr(0,termios.TCSANOW,old);os.write(1,b'\\x1b[?1003l')\nPath("+repr(str(capture))+").write_bytes(data)\n")
    subprocess.run(['xdotool','windowactivate','--sync',terminal,'type','--clearmodifiers','python3 '+str(script)],check=True);subprocess.run(['xdotool','key','Return'],check=True)
    wait(ready.exists);subprocess.run(['xdotool','key','--clearmodifiers','shift+Down','shift+Up'],check=True);function_key('F2');subprocess.run(['xdotool','key','ctrl+shift+l','z','mousemove',str(int(geometry['X'])+120),str(int(geometry['Y'])+120),'click','1','click','4','key','ctrl+shift+l'],check=True);subprocess.run(['xdotool','key','ctrl+shift+y'],check=True);wait(capture.exists)
    expected=b'\x1b[1;2B\x1b[1;2A\x1bOQcustom'
    assert capture.read_bytes()==expected, 'Unexpected Shift arrow bytes: '+capture.read_bytes().hex()
    print('PASS: SSH PTY Shift arrows, custom text shortcut and input lock suppressing keys, raw mouse clicks/motion and wheel',flush=True)
    panel=home/'gui-mc';panel.mkdir(exist_ok=True);selected=home/'gui-mc-selected';selected.unlink(missing_ok=True)
    for name in ('.mc.menu','a.txt','b.txt','c.txt'):
     path=panel/name
     path.write_text('x Record selected files\n    printf \'%%s\' %s > '+str(selected)+'\n' if name=='.mc.menu' else 'synthetic MC fixture\n')
     os.chown(path,user.pw_uid,user.pw_gid)
    os.chown(panel,user.pw_uid,user.pw_gid)
    subprocess.run(['xdotool','type','--clearmodifiers','mc -x '+str(panel)+' '+str(panel)],check=True);subprocess.run(['xdotool','key','Return'],check=True);time.sleep(.8)
    subprocess.run(['xdotool','key','Down','shift+Down'],check=True);function_key('F2');time.sleep(.3);subprocess.run(['xdotool','key','x'],check=True)
    wait(selected.exists);names=selected.read_text().splitlines();assert len(names)==1 and Path(names[0]).name=='.mc.menu',names
    print('PASS: Shift+Down marks a file in a real Midnight Commander panel',flush=True)
    import gi
    gi.require_version('Gdk','3.0');from gi.repository import Gdk
    screen=Gdk.Screen.get_default();picture=Gdk.pixbuf_get_from_window(screen.get_root_window(),0,0,screen.get_width(),screen.get_height());picture.savev(str(args.output/'mc.png'),'png',[],[])
    function_key('F10')
    old_shell=int(shellpid.read_text());connection=int(Path('/proc/'+str(old_shell)+'/stat').read_text().rpartition(')')[2].split()[1])
    assert 'sshd' in Path('/proc/'+str(connection)+'/comm').read_text()
    os.kill(connection,signal.SIGKILL)
    wait(lambda:count.exists() and len(count.read_text().splitlines())>=2,25)
    assert int(shellpid.read_text())!=old_shell
    print('PASS: automatic reconnect after loss of the established SSH connection',flush=True)

   except Exception:
    import gi
    gi.require_version('Gdk','3.0')
    from gi.repository import Gdk
    screen=Gdk.Screen.get_default();picture=Gdk.pixbuf_get_from_window(screen.get_root_window(),0,0,screen.get_width(),screen.get_height());picture.savev(str(args.output/'failure.png'),'png',[],[])
    print('Visible buttons:',[n.name for n in nodes(root) if n.getRoleName()=='push button'],flush=True)
    raise
   finally:
    os.killpg(process.pid,signal.SIGTERM);process.wait(timeout=10)
    # The terminal has its own process group; close only its synthetic session.
    windows=subprocess.run(['xdotool','search','--name','kittyng-test@'],capture_output=True,text=True).stdout.split()
    for window in windows:subprocess.run(['xdotool','windowclose',window],check=False)
finally:
 if not args.existing_account:subprocess.run(['usermod','-p','*',account],check=True)
 marker.unlink(missing_ok=True)
 remote_file.unlink(missing_ok=True)
 for path in [home/name for name in ('gui-command','gui-key-bytes','gui-key-ready','gui-input-capture.py','gui-mc-selected','gui-connections','gui-shell-pid')]:path.unlink(missing_ok=True)
 panel=home/'gui-mc'
 if panel.exists():
  for path in panel.iterdir():path.unlink()
  panel.rmdir()
