"""Native Linux screenshots of synthetic profiles; owned window pixels only."""
import argparse,os,subprocess,tempfile,time,signal
from pathlib import Path
import gi
gi.require_version('Gdk','3.0');gi.require_version('GdkX11','3.0')
from gi.repository import Gdk,GdkX11
p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args();args.output.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='kitty-ng-shots-') as folder:
 root=Path(folder);(root/'Sessions/Work').mkdir(parents=True)
 for name in ('demo-server','Work/demo-backup'):(root/'Sessions'/name).write_text('HostName\\example.invalid\\\nProtocol\\ssh\\\nPortNumber\\22\\\n')
 for lang in ('en','ru'):
  for kind,flags,pattern in [('session',[],'^KiTTY-ng$'),('kittygen',['--keygen'],'^KiTTYgen')]:
   env={**os.environ,'KITTY_NG_PORTABLE_DIR':str(root),'KITTY_NG_LANGUAGE':lang}
   child=subprocess.Popen([str(args.binary),*flags],env=env,start_new_session=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
   try:
    ids=[]
    for attempt in range(100):
     result=subprocess.run(['xdotool','search','--onlyvisible','--name',pattern],capture_output=True,text=True);ids=result.stdout.split()
     if ids:break
     time.sleep(.1)
    assert ids,'Window did not appear'
    time.sleep(1)
    wid=ids[-1]
    subprocess.run(['xdotool','windowmove','--sync',wid,'0','0'],check=True)
    time.sleep(.3)
    geometry=dict(line.split('=',1) for line in subprocess.check_output(['xdotool','getwindowgeometry','--shell',wid],text=True).splitlines() if '=' in line)
    screen=Gdk.Screen.get_default();window=screen.get_root_window()
    picture=Gdk.pixbuf_get_from_window(window,int(geometry['X']),int(geometry['Y']),int(geometry['WIDTH']),int(geometry['HEIGHT']));assert picture
    picture.savev(str(args.output/f'linux-{kind}-{lang}.png'),'png',[],[])
   finally:os.killpg(child.pid,signal.SIGTERM);child.wait(timeout=10)
print('Captured four native Linux windows')
