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
  channel.send(b"\r\nSERVER RAW: Access denied\r\n")
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
 if '--winscp-only' in sys.argv:
  settings=root/'profiles';settings.mkdir(exist_ok=True)
  external=settings/'Программа с пробелами';external.mkdir()
  target=external/'WinSCP.exe';shutil.copy2(probe,target)
  ini=settings/'kitty.ini';ini.write_text('[KiTTY]\nWinSCPPath=Программа с пробелами/WinSCP.exe\nshortcuts=yes\n',encoding='utf-16')
  env['KITTY_NG_PROBE_WINSCP']='1'
  for name in ('ProgramFiles','ProgramFiles(x86)','ProgramW6432'): env[name]=str(root/'absent-installation')
 if '--quick-only' in sys.argv:
  settings=root/'profiles';settings.mkdir(exist_ok=True)
  (settings/'kitty.ini').write_text('[KiTTY]\nunknown=keep\n[QuickCommands]\ncmd1_name=Тест & один\ncmd1_instruction=echo Привет\ncmd1_enter=no\ncmd2_name=Test two\ncmd2_instruction=echo second\ncmd2_enter=yes\n',encoding='utf-16')
 thread=threading.Thread(target=serve,daemon=True);thread.start()
 key_path=root/'Ключи с пробелами'/'проверка.ppk'
 log_args=['-i',str(key_path)] if '--eventlog-only' in sys.argv else []
 process=subprocess.Popen([str(client),*(['-load','Legacy'] if '--legacy-arrows-only' in sys.argv else []),'--portable-dir',str(root/'profiles'),'--language',('en' if '--english' in sys.argv else 'ru'),'-ssh','127.0.0.1','-P',str(listener.getsockname()[1]),'-l','test','-pw','synthetic','-hostkey',fp,*log_args],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 try:
  assert ready.wait(15),failure
  hwnd=until(lambda:next((h for h in windows(process.pid) if 'KiTTY-ng' in title(h) and u.GetDlgItem(h,3001)==None),None))
  if '--quick-only' in sys.argv:
   u.GetSystemMenu.argtypes=[w.HWND,w.BOOL];u.GetSystemMenu.restype=w.HMENU
   u.GetSubMenu.argtypes=[w.HMENU,ctypes.c_int];u.GetSubMenu.restype=w.HMENU
   u.GetMenuItemID.argtypes=[w.HMENU,ctypes.c_int];u.GetMenuItemID.restype=w.UINT
   u.GetMenuItemCount.argtypes=[w.HMENU]
   parent=u.GetSystemMenu(hwnd,False)
   entries=[(i,u.GetSubMenu(parent,i)) for i in range(u.GetMenuItemCount(parent))]
   quick_index,quick=next((i,m) for i,m in entries if m and u.GetMenuItemID(m,0)==0x900)
   special_index=next(i for i,m in entries if m and 0x400<=u.GetMenuItemID(m,0)<0x800)
   assert quick_index==special_index+1,(special_index,quick_index)
   u.SendMessageW(hwnd,0x117,quick,0)
   u.SendMessageW(hwnd,0x112,0x240,0) # lock input
   u.SendMessageW(hwnd,0x112,0x900,0);time.sleep(.15);assert not received,received
   u.SendMessageW(hwnd,0x112,0x240,0)
   u.SendMessageW(hwnd,0x112,0x900,0)
   until(lambda:len(received)>=len('echo Привет'.encode()))
   assert bytes(received)=='echo Привет'.encode(),received
   u.SendMessageW(hwnd,0x112,0x910,0)
   expected='echo Приветecho second\r'.encode()
   until(lambda:len(received)>=len(expected));assert bytes(received)==expected,received
   print('PASS: quick commands order, literal UTF-8, optional Enter and input lock',flush=True)
  elif '--settings-layout' in sys.argv:
   from windows_settings_layout import verify
   verify(process,hwnd,u,windows,title,until,'--english' in sys.argv)
  elif '--winscp-only' in sys.argv:
   def launched():
    def recorded():
     if process.poll() is not None: raise AssertionError((process.returncode,process.stderr.read().decode(errors='replace')))
     dialogs=[h for h in windows(process.pid) if h!=hwnd and title(h)=='KiTTY-ng']
     if dialogs:
      u.SendMessageW(dialogs[0],0x111,1,0)
      raise AssertionError('Launch displayed an error dialog')
     return report.exists() and report.stat().st_size>0
    try: until(recorded)
    except AssertionError:
     print('Synthetic client windows:',[(h,title(h)) for h in windows(process.pid)],flush=True)
     if process.poll() is None: process.terminate();process.wait(5)
     print(process.stderr.read().decode(errors='replace'),flush=True)
     raise
    parts=[x.decode('utf8') for x in report.read_bytes().split(b'\0')[:-1]]
    assert parts[0]==str(target),parts
    assert parts[1]==f'sftp://127.0.0.1:{listener.getsockname()[1]}/',parts
    assert '/username=test' in parts and all('synthetic' not in x for x in parts),parts
    report.unlink()
   u.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
   u.PostMessageW(hwnd,0x112,0x290,0);launched()
   kernel=ctypes.WinDLL('kernel32');current=kernel.GetCurrentThreadId();tid=u.GetWindowThreadProcessId(hwnd,None)
   assert u.AttachThreadInput(current,tid,True)
   keyboard=(ctypes.c_ubyte*256)();original=(ctypes.c_ubyte*256)();u.GetKeyboardState(original);keyboard[0x10]=0x80
   try:
    u.SetKeyboardState(keyboard);u.SendMessageW(hwnd,0x100,0x72,0x01000001)
   finally:
    u.SetKeyboardState(original);u.AttachThreadInput(current,tid,False)
   launched()
   # Without a configured path, the portable-root executable is discovered.
   target=settings/'WinSCP.exe';shutil.copy2(probe,target);ini.write_text('[KiTTY]\n',encoding='utf-16')
   u.SendMessageW(hwnd,0x112,0x290,0);launched()
   # Simulate a standard installed WinSCP without touching real installations.
   installed=root/'absent-installation/WinSCP';installed.mkdir(parents=True)
   target=installed/'WinSCP.exe';shutil.copy2(probe,target)
   u.SendMessageW(hwnd,0x112,0x290,0);launched()
   ini.write_text('[KiTTY]\nWinSCPPath=%ProgramFiles%/WinSCP/WinSCP.exe\n',encoding='utf-16')
   u.SendMessageW(hwnd,0x112,0x290,0);launched()
   # An explicitly unavailable executable is preserved, never silently replaced.
   ini.write_text('[KiTTY]\nWinSCPPath=missing.exe\n',encoding='utf-16');before=ini.read_bytes()
   u.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
   u.PostMessageW(hwnd,0x112,0x290,0)
   dialog=until(lambda:next((h for h in windows(process.pid) if title(h)=='KiTTY-ng' and h!=hwnd),None))
   u.SendMessageW(dialog,0x10,0,0);until(lambda:not u.IsWindow(dialog));assert ini.read_bytes()==before and not report.exists()
   wrapper=settings/'wrapper.bat';wrapper.write_text('@echo off\nexit /b 0\n')
   ini.write_text('[KiTTY]\nWinSCPPath=wrapper.bat\n',encoding='utf-16');before=ini.read_bytes()
   u.PostMessageW(hwnd,0x112,0x290,0)
   dialog=until(lambda:next((h for h in windows(process.pid) if title(h)=='KiTTY-ng' and h!=hwnd),None))
   u.SendMessageW(dialog,0x10,0,0);until(lambda:not u.IsWindow(dialog));assert ini.read_bytes()==before and not report.exists()
   print('PASS: WinSCP menu and Shift+F3; relative Unicode path from portable root; installed executable discovery; exact argv without saved password; unavailable path preserved')
  elif '--tray-only' in sys.argv:
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
  elif '--eventlog-only' in sys.argv:
   u.SendMessageW(hwnd,0x112,0x10,0)
   log=until(lambda:next((h for h in windows(process.pid) if u.GetDlgItem(h,1001)),None))
   listing=u.GetDlgItem(log,1001);texts=[]
   for i in range(u.SendMessageW(listing,0x18b,0,0)):
    b=ctypes.create_unicode_buffer(8192);u.SendMessageW(listing,0x189,i,ctypes.addressof(b));texts.append(b.value)
   joined='\n'.join(texts)
   assert str(key_path) in joined and '\ufffd' not in joined, joined
   for phrase in (['Reading key file','Access granted','Using SSH protocol version'] if '--english' in sys.argv else ['Чтение файла ключа','Доступ разрешён','Используется протокол SSH']):assert phrase in joined,joined
   if '--english' not in sys.argv:
    for phrase in ['Reading key file','Access granted','Using SSH protocol version','We claim version','Initialised']:assert phrase not in joined,joined
   u.SendMessageW(listing,0x185,1,-1);u.SendMessageW(log,0x111,1002,0)
   kernel=ctypes.WinDLL('kernel32');kernel.GlobalLock.argtypes=[w.HANDLE];kernel.GlobalLock.restype=ctypes.c_void_p
   kernel.GlobalUnlock.argtypes=[w.HANDLE];u.GetClipboardData.argtypes=[w.UINT];u.GetClipboardData.restype=w.HANDLE
   assert u.OpenClipboard(None)
   try:
    handle=u.GetClipboardData(13);assert handle;memory=kernel.GlobalLock(handle);assert memory
    try:copied=ctypes.wstring_at(memory)
    finally:kernel.GlobalUnlock(handle)
   finally:u.CloseClipboard()
   assert str(key_path) in copied and '\ufffd' not in copied,copied
   def terminal_text():
    u.SendMessageW(hwnd,0x112,0x170,0)
    if not u.OpenClipboard(None):return ''
    try:
     handle=u.GetClipboardData(13)
     if not handle:return ''
     memory=kernel.GlobalLock(handle)
     if not memory:return ''
     try:return ctypes.wstring_at(memory)
     finally:kernel.GlobalUnlock(handle)
    finally:u.CloseClipboard()
   screen=until(lambda:(text if 'SERVER RAW: Access denied' in (text:=terminal_text()) else None))
   expected='Using username "test".' if '--english' in sys.argv else 'Имя пользователя: «test».'
   assert expected in screen,screen
   assert ('Unable to use key file' if '--english' in sys.argv else 'Не удалось использовать файл ключа') in screen,screen
   print('PASS: native event log, Unicode clipboard and login messages use selected language; server payload unchanged')
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
  if not any(x in sys.argv for x in ('--quick-only','--settings-layout','--download-only','--seed-only','--tray-only','--legacy-arrows-only','--winscp-only','--eventlog-only')): print('PASS: actual terminal menu launches both matching x64 probes; Windows Shift+arrows reach SSH; Cyrillic event log is readable')
 finally:
  done.set();process.terminate();process.communicate(timeout=10)
  for transport in connection:transport.close()
  listener.close();thread.join(3)
assert not failure,failure
