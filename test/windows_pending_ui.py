"""Isolated native Windows active restart, tab reconfiguration and tray callbacks."""
import ctypes,os,socket,subprocess,sys,tempfile,threading,time
from ctypes import wintypes as w
from pathlib import Path
u=ctypes.WinDLL('user32',use_last_error=True)
for name,args,result in [('PostMessageW',[w.HWND,w.UINT,w.WPARAM,w.LPARAM],w.BOOL),('GetParent',[w.HWND],w.HWND),('GetWindowLongW',[w.HWND,ctypes.c_int],w.LONG)]:
 f=getattr(u,name);f.argtypes=args;f.restype=result
callback=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
def windows():
 out=[]
 @callback
 def visit(h,_):out.append(h);return True
 u.EnumWindows(visit,0);return out
def children(parent):
 out=[]
 @callback
 def visit(h,_):out.append(h);return True
 u.EnumChildWindows(parent,visit,0);return out
def title(h):
 b=ctypes.create_unicode_buffer(512);u.GetWindowTextW(h,b,512);return b.value
def cls(h):
 b=ctypes.create_unicode_buffer(256);u.GetClassNameW(h,b,256);return b.value
def pid(h):
 value=w.DWORD();u.GetWindowThreadProcessId(h,ctypes.byref(value));return value.value
def wait(fn):
 end=time.monotonic()+12
 while time.monotonic()<end:
  value=fn()
  if value:return value
  time.sleep(.05)
 raise AssertionError('Native regression timed out: '+str(fn))
listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen();listener.settimeout(.2)
connections=[];stop=threading.Event()
def accept():
 while not stop.is_set():
  try:
   c,_=listener.accept();connections.append(c);c.sendall(b'Isolated raw connection\r\n')
  except socket.timeout:pass
  except OSError:break
threading.Thread(target=accept,daemon=True).start()
base=Path(sys.argv[1]).resolve();processes=[]
with tempfile.TemporaryDirectory(prefix='kitty-ng-ui-regression-') as directory:
 env=dict(os.environ,KITTY_NG_PORTABLE_DIR=directory)
 Path(directory,'kitty.ini').write_text('[KiTTY]\nUITheme=2\n',encoding='utf8')
 try:
  process=subprocess.Popen([str(base/'kitty-ng-x64.exe'),'--portable-dir',directory,'--language','en','-raw','127.0.0.1','-P',str(listener.getsockname()[1])],env=env);processes.append(process)
  page=wait(lambda:next((h for h in windows() if pid(h)==process.pid and cls(h)=='KiTTY-ng'),None));wait(lambda:len(connections)==1)
  menu=u.GetSystemMenu(page,False);assert not u.GetMenuState(menu,0x40,0)&3,'Active restart must be enabled'
  u.PostMessageW(page,0x112,0x40,0);wait(lambda:len(connections)==2)
  print('PASS: explicit active-session restart retains window',flush=True)
  def tray(hwnd,message):
   for event in (0x205,(1<<16)|0x7B):
    u.PostMessageW(hwnd,message,1,event)
    wait(lambda:any(cls(h)=='#32768' and pid(h)==pid(hwnd) for h in windows()))
    u.PostMessageW(hwnd,0x1F,0,0)
    wait(lambda:not any(cls(h)=='#32768' and pid(h)==pid(hwnd) for h in windows()))
  u.PostMessageW(page,0x112,0x1B0,0);wait(lambda:not u.IsWindowVisible(page))
  tray(page,0x8000+101)
  u.PostMessageW(page,0x8000+101,1,0x202);wait(lambda:u.IsWindowVisible(page))
  print('PASS: client tray menu accepts legacy and packed context events',flush=True)
  u.PostMessageW(page,0x112,0x30,0)
  host=wait(lambda:u.GetParent(page));process_ids={pid(host),process.pid}
  wait(lambda:len(connections)==3)
  u.PostMessageW(page,0x112,0x50,0)
  dialog=wait(lambda:next((h for h in windows() if pid(h)==process.pid and cls(h)=='PuTTYConfigBox'),None))
  apply=next(h for h in children(dialog) if cls(h)=='Button' and title(h).replace('&','')=='Apply')
  u.SendMessageW=ctypes.WINFUNCTYPE(ctypes.c_ssize_t,w.HWND,w.UINT,w.WPARAM,w.LPARAM)(('SendMessageW',u))
  u.SetForegroundWindow(dialog)
  u.PostMessageW(dialog,0x111,u.GetDlgCtrlID(apply),apply);wait(lambda:not u.IsWindow(dialog))
  assert not u.GetWindowLongW(page,-20)&0x200,'Applying settings restored a client edge inside tabs'
  assert not u.GetWindowLongW(page,-16)&0x40000,'Applying settings restored a thick frame inside tabs'
  print('PASS: applying live settings preserves borderless tab page',flush=True)
  u.PostMessageW(host,0x8000+0x4B2,0x1B0,0);wait(lambda:not u.IsWindowVisible(host))
  tray(host,0x8000+0x4B3)
  print('PASS: tab-host tray context menu opens repeatedly',flush=True)
 except Exception:
  print([(cls(h),title(h),[(cls(c),title(c),u.GetDlgCtrlID(c)) for c in children(h)]) for h in windows() if pid(h) in ({process.pid,pid(host)} if 'host' in locals() else {process.pid})],flush=True)
  raise
 finally:
  stop.set();listener.close()
  for c in connections:c.close()
  for p in processes:
   subprocess.run(['taskkill','/PID',str(p.pid),'/T','/F'],capture_output=True)
  if 'host' in locals():
   subprocess.run(['taskkill','/PID',str(pid(host)),'/T','/F'],capture_output=True)
