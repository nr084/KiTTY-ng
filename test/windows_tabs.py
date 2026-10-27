"""Native Windows tabs regression against independent loopback SSH connections."""
import base64,ctypes,hashlib,json,logging,os,socket,subprocess,sys,tempfile,threading,time
from pathlib import Path
from ctypes import wintypes as w
import paramiko
logging.getLogger('paramiko').setLevel(logging.CRITICAL)
u=ctypes.WinDLL('user32',use_last_error=True)
u.SetProcessDPIAware()
for name,args,result in [('SendMessageW',[w.HWND,w.UINT,w.WPARAM,w.LPARAM],ctypes.c_ssize_t),('PostMessageW',[w.HWND,w.UINT,w.WPARAM,w.LPARAM],w.BOOL),('GetParent',[w.HWND],w.HWND),('GetMenu',[w.HWND],w.HMENU)]:
 f=getattr(u,name);f.argtypes=args;f.restype=result
callback=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
def enum(parent=None):
 out=[]
 @callback
 def each(h,_):out.append(h);return True
 if parent:u.EnumChildWindows(parent,each,0)
 else:u.EnumWindows(each,0)
 return out
def cls(h):
 b=ctypes.create_unicode_buffer(256);u.GetClassNameW(h,b,256);return b.value
def title(h):
 b=ctypes.create_unicode_buffer(512);u.GetWindowTextW(h,b,512);return b.value
def pid(h):
 p=w.DWORD();u.GetWindowThreadProcessId(h,ctypes.byref(p));return p.value
def wait(fn,seconds=15):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  result=fn()
  if result:return result
  time.sleep(.05)
 raise AssertionError('Timed out: '+str(fn))
key=paramiko.RSAKey.generate(2048);fingerprint='SHA256:'+base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip('=')
listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen();listener.settimeout(.2)
channels=[];transports=[];received={};done=threading.Event()
class Server(paramiko.ServerInterface):
 def check_auth_none(self,user):return paramiko.AUTH_SUCCESSFUL if user=="demo" else paramiko.AUTH_FAILED
 def check_auth_password(self,user,password):return paramiko.AUTH_SUCCESSFUL if (user,password)==('demo','synthetic') else paramiko.AUTH_FAILED
 def get_allowed_auths(self,user):return 'password'
 def check_channel_request(self,*args):return paramiko.OPEN_SUCCEEDED
 def check_channel_pty_request(self,*args):return True
 def check_channel_shell_request(self,*args):return True
 def check_channel_window_change_request(self,*args):return True
def connection(sock):
 try:
  t=paramiko.Transport(sock);transports.append(t);t.add_server_key(key);t.start_server(server=Server());c=t.accept(15)
  if c is None:return
  index=len(channels)+1;channels.append(c);received[index]=bytearray();c.send(f'\x1b]0;Demo session {index}\x07\r\nIndependent SSH session {index}\r\n$ '.encode())
  while not done.is_set():
   data=c.recv(4096)
   if not data:break
   received[index].extend(data);c.send(data)
 except (EOFError,OSError,paramiko.SSHException):pass
def accept():
 while not done.is_set():
  try:s,_=listener.accept();threading.Thread(target=connection,args=(s,),daemon=True).start()
  except socket.timeout:pass
  except OSError:
   if done.is_set():break
   raise
threading.Thread(target=accept,daemon=True).start()
class GuiThreadInfo(ctypes.Structure):
 _fields_=[('cbSize',w.DWORD),('flags',w.DWORD),('active',w.HWND),('focus',w.HWND),('capture',w.HWND),('menu',w.HWND),('move',w.HWND),('caret',w.HWND),('rect',w.RECT)]

class KeyboardInput(ctypes.Structure):
 _fields_=[('vk',w.WORD),('scan',w.WORD),('flags',w.DWORD),('time',w.DWORD),('extra',ctypes.c_size_t)]
class InputUnion(ctypes.Union):
 _fields_=[('key',KeyboardInput),('mouse_padding',ctypes.c_byte*(32 if ctypes.sizeof(ctypes.c_void_p)==8 else 24))]
class Input(ctypes.Structure):
 _fields_=[('type',w.DWORD),('data',InputUnion)]
u.SendInput.argtypes=[w.UINT,ctypes.POINTER(Input),ctypes.c_int];u.SendInput.restype=w.UINT

def keyboard(page,index,letter):
 # Real keyboard routing: posting WM_CHAR directly bypasses focus bugs.
 info=GuiThreadInfo();info.cbSize=ctypes.sizeof(info)
 thread=u.GetWindowThreadProcessId(page,None)
 time.sleep(.2)
 assert u.GetGUIThreadInfo(thread,ctypes.byref(info))
 assert info.focus==page, f'Keyboard focus lost: expected {page:#x}, got {info.focus}; active={info.active}'
 before={i:bytes(data) for i,data in received.items()}
 events=(Input*2)()
 for i,event in enumerate(events):
  event.type=1;event.data.key=KeyboardInput(0,ord(letter),4|(2 if i else 0),0,0)
 assert u.SendInput(2,events,ctypes.sizeof(Input))==2
 wait(lambda:bytes(received[index])==before[index]+letter.encode())
 assert all(bytes(received[i])==data for i,data in before.items() if i!=index), 'Input reached inactive connection'

binary=Path(sys.argv[1]).resolve();pids=set();processes=[]
with tempfile.TemporaryDirectory(prefix='tabs-runtime-',dir=binary.parent.parent) as folder:
 root=Path(folder);(root/'Sessions').mkdir();(root/'Sessions/Demo').write_text('WarnOnClose\\1\\\n')
 def launch():
  p=subprocess.Popen([str(binary),'--portable-dir',str(root),'--language','en','-load','Demo','-ssh','127.0.0.1','-P',str(listener.getsockname()[1]),'-l','demo','-pw','synthetic','-hostkey',fingerprint]);processes.append(p);pids.add(p.pid);return p
 try:
  p=launch();first=wait(lambda:next((h for h in enum() if pid(h)==p.pid and title(h)=='Demo session 1'),None))
  assert not any(cls(h)=='SysTabControl32' and u.IsWindowVisible(h) for h in enum(first))
  u.PostMessageW(first,0x112,0x30,0)
  host=wait(lambda:next((h for h in enum() if cls(h)=='KiTTYng.SessionTabs' and u.GetParent(first)==h),None));pids.add(pid(host))
  strip=wait(lambda:next((h for h in enum(host) if cls(h)=='SysTabControl32'),None))
  wait(lambda:u.SendMessageW(strip,0x1304,0,0)==2)
  second=wait(lambda:next((h for h in enum(host) if title(h)=='Demo session 2'),None));pids.add(pid(second))
  assert u.IsWindowVisible(second) and not u.IsWindowVisible(first)
  u.PostMessageW(second,0x102,ord('B'),0);wait(lambda:received.get(2)==b'B');assert received[1]==b''
  scale=u.GetDpiForWindow(host)/96
  u.GetForegroundWindow.restype=w.HWND
  u.SetForegroundWindow.argtypes=[w.HWND]
  u.SetForegroundWindow(host)
  # A preceding GUI suite may own foreground permission. Activate through the
  # title bar if necessary, without selecting or dragging a tab.
  rect=w.RECT();u.GetWindowRect(host,ctypes.byref(rect));u.SetCursorPos(rect.left+100,rect.top+10)
  if u.GetForegroundWindow()!=host:
   u.mouse_event(2,0,0,0,0);u.mouse_event(4,0,0,0,0)
  wait(lambda:u.GetForegroundWindow()==host)
  def mouse(message,x,y=15,buttons=0):
   u.SendMessageW(strip,message,buttons,(round(y*scale)<<16)|round(x*scale))
   time.sleep(.1)
  mouse(0x201,35,buttons=1);mouse(0x200,250,buttons=1);mouse(0x202,250)
  wait(lambda:u.SendMessageW(strip,0x130b,0,0)==1)
  wait(lambda:u.IsWindowVisible(first));assert len(channels)==2
  keyboard(first,1,'x')
  mouse(0x201,250,buttons=1);mouse(0x200,35,buttons=1);mouse(0x202,35)
  wait(lambda:u.SendMessageW(strip,0x130b,0,0)==0)
  mouse(0x201,250,buttons=1);mouse(0x202,250)
  wait(lambda:u.IsWindowVisible(second))
  keyboard(second,2,'y')
  for x,page,index,letter in [(35,first,1,'a'),(250,second,2,'b')]*2:
   mouse(0x201,x,buttons=1);mouse(0x202,x)
   keyboard(page,index,letter)
  if '--screenshot' in sys.argv:
   from PIL import ImageGrab
   target=Path(sys.argv[sys.argv.index('--screenshot')+1]);target.parent.mkdir(parents=True,exist_ok=True)
   u.SetForegroundWindow(host);time.sleep(.4);rect=w.RECT();u.GetWindowRect(host,ctypes.byref(rect))
   ImageGrab.grab(bbox=(rect.left,rect.top,rect.right,rect.bottom)).save(target)

  # Window commands must continue to target the containing window.
  u.GetWindowLongPtrW.argtypes=[w.HWND,ctypes.c_int];u.GetWindowLongPtrW.restype=ctypes.c_ssize_t
  u.PostMessageW(second,0x112,0x180,0);wait(lambda:not (u.GetWindowLongPtrW(host,-16)&0xC00000))
  u.PostMessageW(second,0x112,0x180,0);wait(lambda:u.GetWindowLongPtrW(host,-16)&0xC00000)
  u.PostMessageW(second,0x112,0x230,0);wait(lambda:u.GetWindowLongPtrW(host,-20)&8)
  u.PostMessageW(second,0x112,0x230,0);wait(lambda:not (u.GetWindowLongPtrW(host,-20)&8))
  u.PostMessageW(second,0x112,0x1B0,0);wait(lambda:not u.IsWindowVisible(host))
  u.PostMessageW(host,0x8000+0x4B3,1,0x202);wait(lambda:u.IsWindowVisible(host))
  # Exercise the visible red close icon on a third tab, not a synthetic command.
  u.PostMessageW(second,0x112,0x30,0)
  third=wait(lambda:next((h for h in enum(host) if title(h)=='Demo session 3'),None));pids.add(pid(third))
  wait(lambda:u.SendMessageW(strip,0x1304,0,0)==3)
  from PIL import ImageGrab
  u.SetForegroundWindow(host);time.sleep(.3);rect=w.RECT();u.GetWindowRect(strip,ctypes.byref(rect))
  pixels=ImageGrab.grab(bbox=(rect.left,rect.top,rect.right,rect.bottom)).convert('RGB')
  columns=[]
  for x in range(pixels.width):
   if sum(1 for y in range(pixels.height) if (lambda c:c[0]>150 and c[0]>c[1]*1.5 and c[0]>c[2]*1.5)(pixels.getpixel((x,y))))>3:columns.append(x)
  groups=[]
  for x in columns:
   if not groups or x>groups[-1][-1]+1:groups.append([])
   groups[-1].append(x)
  assert len(groups)==3, 'Each tab must have its own red close icon'
  x=sum(groups[-1])//len(groups[-1]);y=pixels.height//2
  u.SendMessageW(strip,0x201,1,(y<<16)|x);u.SendMessageW(strip,0x202,0,(y<<16)|x)
  wait(lambda:not u.IsWindow(third));wait(lambda:u.SendMessageW(strip,0x1304,0,0)==2)
  keyboard(second,2,'r')
  assert not any(cls(h)=='#32770' and pid(h) in pids for h in enum())
  u.SendMessageW(strip,0x201,1,(round(15*scale)<<16)|round(250*scale));u.SendMessageW(strip,0x202,0,(round(15*scale)<<16)|round(250*scale))
  def tab_menu(command_index):
   point=w.POINT(25,12);u.ClientToScreen(strip,ctypes.byref(point));u.SetCursorPos(point.x,point.y)
   u.PostMessageW(strip,0x204,2,(12<<16)|25);u.PostMessageW(strip,0x205,0,(12<<16)|25)
   popup=wait(lambda:next((h for h in enum() if cls(h)=='#32768' and pid(h)==pid(host)),None))
   menu=u.SendMessageW(popup,0x1e1,0,0)
   caption=ctypes.create_unicode_buffer(100)
   u.GetMenuStringW.argtypes=[w.HMENU,w.UINT,w.LPWSTR,ctypes.c_int,w.UINT]
   u.GetMenuStringW(menu,0,caption,100,0x400);assert caption.value=="Close tab",caption.value
   u.GetMenuState.argtypes=[w.HMENU,w.UINT,w.UINT]
   assert u.GetMenuState(menu,2,0x400)&3, 'First tab must not move left'
   rect=w.RECT();u.GetMenuItemRect.argtypes=[w.HWND,w.HMENU,w.UINT,ctypes.POINTER(w.RECT)]
   assert u.GetMenuItemRect(None,menu,command_index,ctypes.byref(rect))
   u.SetCursorPos((rect.left+rect.right)//2,(rect.top+rect.bottom)//2)
   u.mouse_event(2,0,0,0,0);u.mouse_event(4,0,0,0,0)
   wait(lambda:not u.IsWindowVisible(popup))
  tab_menu(3)
  wait(lambda:u.SendMessageW(strip,0x130b,0,0)==0)
  assert u.IsWindowVisible(second) and not u.IsWindowVisible(first)
  assert len(channels)==3, 'Moving must not reconnect'
  # Close via the context menu even though WarnOnClose is enabled.
  tab_menu(0);wait(lambda:not u.IsWindow(second))
  wait(lambda:u.SendMessageW(strip,0x1304,0,0)==1)
  assert not any(cls(h)=='#32770' and pid(h) in pids for h in enum())
  # Closing the current tab uses the actual close button, with WarnOnClose=1.
  wait(lambda:u.SendMessageW(strip,0x1304,0,0)==1)
  wait(lambda:not u.IsWindowVisible(strip) and u.IsWindowVisible(first))
  assert not any(cls(h)=='#32770' and pid(h) in pids for h in enum())
  keyboard(first,1,'z')
  # Whole-window close must still warn; Cancel keeps the connection alive.
  u.PostMessageW(host,0x10,0,0)
  dialog=wait(lambda:next((h for h in enum() if pid(h)==pid(host) and cls(h)=='#32770'),None))
  u.PostMessageW(dialog,0x111,2,0);wait(lambda:not u.IsWindow(dialog));assert u.IsWindow(first)
  keyboard(first,1,'q')
  u.PostMessageW(host,0x111,0x7101,0);wait(lambda:not u.IsWindow(host))
  (root/'kitty.ini').write_text('[KiTTY]\nSessionTabs=0\n',encoding='utf-16')
  plain=launch()
  plain_window=wait(lambda:next((h for h in enum() if pid(h)==plain.pid and title(h)=='Demo session 4'),None))
  u.PostMessageW(plain_window,0x112,0x30,0)
  separate=wait(lambda:next((h for h in enum() if title(h)=='Demo session 5'),None));pids.add(pid(separate))
  assert u.GetParent(plain_window) is None and u.GetParent(separate) is None
  assert not any(cls(h)=='KiTTYng.SessionTabs' and pid(h) in pids for h in enum())
  print('PASS: SessionTabs=0 retains independent top-level windows',flush=True)
  print('PASS: independent SSH tabs; bar hidden for one session; active-tab input; mouse drag and context-menu move preserve connections; tab close ignores WarnOnClose; window close still confirms',flush=True)
 finally:
  if "host" in locals() and u.IsWindow(host):
   from PIL import ImageGrab
   u.SetForegroundWindow(host);time.sleep(.3);rect=w.RECT();u.GetWindowRect(host,ctypes.byref(rect));ImageGrab.grab(bbox=(rect.left,rect.top,rect.right,rect.bottom)).save(binary.parent/"tabs-debug.png")
  for h in enum():
   if cls(h)=='KiTTYng.SessionTabs' and pid(h) in pids:
    pids.update(pid(c) for c in enum(h))
  for process_id in pids:
   subprocess.run(['taskkill','/PID',str(process_id),'/T','/F'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
  done.set()
  for t in transports:t.close()
  listener.close()
