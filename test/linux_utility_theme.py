"""Native utility system-theme and About smoke check against shipped AppRun."""
import os,sys,time,tempfile,subprocess,signal
from pathlib import Path
import pyatspi
binary=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
def nodes(n):
 yield n
 for c in n:
  try:yield from nodes(c)
  except Exception:pass
def wait(fn):
 end=time.monotonic()+15
 while time.monotonic()<end:
  try:
   r=fn()
   if r:return r
  except Exception:pass
  time.sleep(.1)
 print([(n.getRoleName(),n.name) for n in visible()],flush=True)
 raise AssertionError('Utility About timeout')
def visible():
 root=pyatspi.Registry.getDesktop(0);root.clearCache()
 return [n for n in nodes(root) if n.getState().contains(pyatspi.STATE_SHOWING)]
for theme in ('Adwaita','Adwaita:dark'):
 for tool in ('--keygen','--agent'):
  with tempfile.TemporaryDirectory() as d:
   env={**os.environ,'GTK_THEME':theme,'KITTY_NG_PORTABLE_DIR':d,'KITTY_NG_LANGUAGE':'en','G_DEBUG':'fatal-criticals'}
   p=subprocess.Popen([str(binary),tool],env=env,start_new_session=True,stdout=subprocess.DEVNULL)
   try:
    button=wait(lambda:next((n for n in visible() if n.name=='About' and n.getRoleName() in ('push button','button')),None))
    r=button.queryComponent().getExtents(pyatspi.DESKTOP_COORDS)
    subprocess.run(['xdotool','mousemove','--sync',str(r.x+r.width//2),str(r.y+r.height//2),'click','1'],check=True)
    def about():
     text='\n'.join(n.name or '' for n in visible())
     return text if 'nr084' in text and 'Build date:' in text and '0.85' in text else None
    text=wait(about);assert 'PuTTY' in text and 'KiTTY' in text
    # GTK supplies native colors; compare rendered light/dark backgrounds separately.
    import gi;gi.require_version('Gdk','3.0');from gi.repository import Gdk
    window=Gdk.get_default_root_window();picture=Gdk.pixbuf_get_from_window(window,0,0,window.get_width(),window.get_height());picture.savev(str(out/(tool[2:]+'-'+theme.replace(':','-')+'.png')),'png',[],[])
    print('PASS',tool,theme,'About date/copyright/version',flush=True)
   finally:
    os.killpg(p.pid,signal.SIGTERM);p.wait(timeout=10)
print('PASS: native GTK utility theme/metadata',flush=True)
