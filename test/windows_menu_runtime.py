"""Exercise actual terminal menu commands, with isolated loopback SSH and helper probes."""
import base64,hashlib,json,os,socket,subprocess,tempfile,threading,time,ctypes,shutil,sys
from ctypes import wintypes as w
from pathlib import Path
import paramiko
import logging
logging.getLogger("paramiko").setLevel(logging.CRITICAL)
binary=Path(sys.argv[1]).resolve();probe=binary.parent/'test_companion_probe.exe'
u=ctypes.WinDLL('user32',use_last_error=True)
u.SendMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM];u.SendMessageW.restype=ctypes.c_ssize_t
u.GetWindowTextW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int]
u.GetDlgItem.argtypes=[w.HWND,ctypes.c_int];u.GetDlgItem.restype=w.HWND
callback=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
def windows(pid):
 out=[]
 @callback
 def each(hwnd,unused):
  actual=w.DWORD();u.GetWindowThreadProcessId(hwnd,ctypes.byref(actual))
  if actual.value==pid:out.append(hwnd)
  return True
 u.EnumWindows(each,0);return out
def title(hwnd):
 b=ctypes.create_unicode_buffer(500);u.GetWindowTextW(hwnd,b,len(b));return b.value
def until(predicate):
 end=time.monotonic()+15
 while time.monotonic()<end:
  value=predicate()
  if value:return value
  time.sleep(.05)
 raise AssertionError('Timeout')
key=paramiko.RSAKey.generate(2048);fp='SHA256:'+base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip('=')
ready=threading.Event();failure=[];received=bytearray();connection=[]
class Server(paramiko.ServerInterface):
 def check_auth_password(self,user,password):return paramiko.AUTH_SUCCESSFUL if (user,password)==('test','synthetic') else paramiko.AUTH_FAILED
 def get_allowed_auths(self,user):return 'password'
 def check_channel_request(self,kind,chanid):return paramiko.OPEN_SUCCEEDED
 def check_channel_pty_request(self,*args):return True
 def check_channel_shell_request(self,channel):ready.set();return True
listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(15)
def serve():
 try:
  client,_=listener.accept();transport=paramiko.Transport(client);connection.append(transport);transport.add_server_key(key);transport.start_server(server=Server());channel=transport.accept(15);assert channel;channel.settimeout(15)
  while True:
   part=channel.recv(4096)
   if not part:break
   received.extend(part)
 except Exception as e:
  if not done.is_set():failure.append(repr(e))
done=threading.Event()
with tempfile.TemporaryDirectory(prefix='windows-menu-',dir=binary.parent.parent) as temp:
 root=Path(temp);install=root/'Клиент с пробелами';install.mkdir();client=install/binary.name;shutil.copy2(binary,client)
 for name in ('kittygen','kageant'):shutil.copy2(probe,install/(name+'-x64.exe'))
 if '--legacy-arrows-only' in sys.argv:
  profiles=root/'profiles/Sessions';profiles.mkdir(parents=True);(profiles/'Legacy').write_text('ShiftedArrowKeys\\0\\\n',encoding='ascii')
 report=root/'probe.argv';env=os.environ.copy();env['KITTY_NG_PROBE_REPORT']=str(report)
 thread=threading.Thread(target=serve,daemon=True);thread.start()
 process=subprocess.Popen([str(client),*(['-load','Legacy'] if '--legacy-arrows-only' in sys.argv else []),'--portable-dir',str(root/'profiles'),'--language','ru','-ssh','127.0.0.1','-P',str(listener.getsockname()[1]),'-l','test','-pw','synthetic','-hostkey',fp],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 try:
  assert ready.wait(15),failure
  hwnd=until(lambda:next((h for h in windows(process.pid) if 'KiTTY-NG' in title(h) and u.GetDlgItem(h,3001)==None),None))
  if '--tray-only' in sys.argv:
   u.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
   u.SendMessageW(hwnd,0x112,0x1b0,0);assert not u.IsWindowVisible(hwnd)
   class GUI(ctypes.Structure):
    _fields_=[('cbSize',w.DWORD),('flags',w.DWORD),*[(x,w.HWND) for x in ('active','focus','capture','menu','move','caret')],('rect',w.RECT)]
   tid=u.GetWindowThreadProcessId(hwnd,None)
   def in_menu():
    info=GUI();info.cbSize=ctypes.sizeof(info);assert u.GetGUIThreadInfo(tid,ctypes.byref(info));return bool(info.flags&4)
   u.PostMessageW(hwnd,0x8000+101,1,0x205);until(in_menu);assert not u.IsWindowVisible(hwnd)
   u.SendMessageW(hwnd,0x1f,0,0);until(lambda:not in_menu())
   u.SendMessageW(hwnd,0x8000+101,1,0x203);until(lambda:u.IsWindowVisible(hwnd))
   print('PASS: actual hidden tray icon callback opens menu on right-click and restores on double-click')
  elif '--legacy-arrows-only' in sys.argv:
   # Local keyboard events exercise the Windows front end, not just the formatter.
   kernel=ctypes.WinDLL('kernel32');current=kernel.GetCurrentThreadId();target=u.GetWindowThreadProcessId(hwnd,None);assert u.AttachThreadInput(current,target,True)
   keyboard=(ctypes.c_ubyte*256)();original=(ctypes.c_ubyte*256)();u.GetKeyboardState(original);keyboard[0x10]=0x80
   try:
    u.SetKeyboardState(keyboard)
    for keycode in (0x26,0x28):u.SendMessageW(hwnd,0x100,keycode,0x01000001)
   finally:
    u.SetKeyboardState(original);u.AttachThreadInput(current,target,False)
   until(lambda:b'\x1b[1;2A\x1b[1;2B' in received)
   print('PASS: Shift+arrows from explicit legacy saved profile reach SSH')
  elif '--seed-only' in sys.argv:
   seed=root/'profiles/kitty-ng-win.rnd';until(lambda:seed.exists() and seed.stat().st_size>0)
   assert not (root/'profiles/putty.rnd').exists()
   print('PASS: Windows uses its own portable seed file')
  elif '--download-only' in sys.argv:
   u.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
   u.PostMessageW(hwnd,0x112,0x1d0,0)
   dialog=until(lambda:next((h for h in windows(process.pid) if u.GetDlgItem(h,3001)),None))
   assert title(dialog)=='Получить файл...',title(dialog)
   u.SendMessageW(dialog,0x111,2,0)
   until(lambda:not u.IsWindow(dialog))
   print('PASS: actual Receive file dialog Russian caption and cancel')
  else:
   for command,name in ((0x270,'kageant'),(0x280,'kittygen')):
    u.SendMessageW(hwnd,0x112,command,0)
    until(lambda:report.exists() and report.stat().st_size>0)
    parts=[x.decode('utf8') for x in report.read_bytes().split(b'\0')[:-1]]
    assert parts==[str(install/(name+'-x64.exe'))],parts
    report.unlink()
   # Local keyboard events exercise the Windows front end, not just the formatter.
   kernel=ctypes.WinDLL('kernel32');current=kernel.GetCurrentThreadId();target=u.GetWindowThreadProcessId(hwnd,None);assert u.AttachThreadInput(current,target,True)
   keyboard=(ctypes.c_ubyte*256)();original=(ctypes.c_ubyte*256)();u.GetKeyboardState(original);keyboard[0x10]=0x80
   try:
    u.SetKeyboardState(keyboard)
    for keycode in (0x26,0x28):u.SendMessageW(hwnd,0x100,keycode,0x01000001)
   finally:
    u.SetKeyboardState(original);u.AttachThreadInput(current,target,False)
   until(lambda:b'\x1b[1;2A\x1b[1;2B' in received)
   u.SendMessageW(hwnd,0x112,0x10,0)
   log=until(lambda:next((h for h in windows(process.pid) if 'Журнал' in title(h)),None))
   # Event log resource list ID is supplied by the header rather than guessed here.
   import re
   header=(Path(__file__).resolve().parents[1]/'windows/putty-rc.h').read_text();identifier=int(re.search(r'#define\s+IDN_LIST\s+(\d+)',header)[1])
   listing=u.GetDlgItem(log,identifier);assert listing
   texts=[]
   for i in range(u.SendMessageW(listing,0x18b,0,0)):
    b=ctypes.create_unicode_buffer(4096);u.SendMessageW(listing,0x189,i,ctypes.addressof(b));texts.append(b.value)
   assert any('Подключ' in x or 'Разреш' in x for x in texts),texts
  if not any(x in sys.argv for x in ('--download-only','--seed-only','--tray-only','--legacy-arrows-only')): print('PASS: actual terminal menu launches both matching x64 probes; Windows Shift+arrows reach SSH; Cyrillic event log is readable')
 finally:
  done.set();process.terminate();process.communicate(timeout=10)
  for transport in connection:transport.close()
  listener.close();thread.join(3)
assert not failure,failure
