"""Native Windows tabs regression against independent loopback SSH connections."""
import struct,base64,ctypes,hashlib,json,logging,os,socket,subprocess,sys,tempfile,threading,time
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
  message=w.MSG()
  while u.PeekMessageW(ctypes.byref(message),None,0,0,1):
   u.TranslateMessage(ctypes.byref(message));u.DispatchMessageW(ctypes.byref(message))
  result=fn()
  if result:return result
  time.sleep(.05)
 raise AssertionError('Timed out: '+str(fn))
key=paramiko.RSAKey.generate(2048);fingerprint='SHA256:'+base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip('=')
listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen();listener.settimeout(.2)
channels=[];transports=[];received={};done=threading.Event()
from duplicate_auth_fixture import PPK,PASSPHRASE,PUBLIC
client_key=paramiko.Ed25519Key(data=base64.b64decode(PUBLIC)) if '--key' in sys.argv else None
authenticated=[]
class Server(paramiko.ServerInterface):
 def __init__(self):self.shell_ready=threading.Event()
 def check_auth_none(self,user):return paramiko.AUTH_FAILED
 def check_auth_password(self,user,password):return paramiko.AUTH_SUCCESSFUL if (user,password)==('demo','synthetic') else paramiko.AUTH_FAILED
 def check_auth_publickey(self,user,key):
  if user=='demo' and client_key is not None and key==client_key:authenticated.append(user);return paramiko.AUTH_SUCCESSFUL
  return paramiko.AUTH_FAILED
 def get_allowed_auths(self,user):return 'publickey' if client_key is not None else 'password'
 def check_channel_request(self,*args):return paramiko.OPEN_SUCCEEDED
 def check_channel_pty_request(self,*args):return True
 def check_channel_shell_request(self,*args):self.shell_ready.set();return True
 def check_channel_window_change_request(self,*args):return True
def connection(sock):
 try:
  t=paramiko.Transport(sock);transports.append(t);t.add_server_key(key);server=Server();t.start_server(server=server);c=t.accept(15)
  if c is None or not server.shell_ready.wait(15):return
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
 assert info.focus==page, f'Keyboard focus lost: expected {page:#x}, got {info.focus}; active={info.active}; foreground={u.GetForegroundWindow()} host={host}'
 before={i:bytes(data) for i,data in received.items()}
 events=(Input*2)()
 for i,event in enumerate(events):
  event.type=1;event.data.key=KeyboardInput(0,ord(letter),4|(2 if i else 0),0,0)
 assert u.SendInput(2,events,ctypes.sizeof(Input))==2
 wait(lambda:bytes(received[index])==before[index]+letter.encode())
 assert all(bytes(received[i])==data for i,data in before.items() if i!=index), 'Input reached inactive connection'

binary=Path(sys.argv[1]).resolve();pids=set();processes=[]
with tempfile.TemporaryDirectory(prefix='tabs-runtime-',dir=binary.parent.parent) as folder:
 root=Path(folder);(root/'Sessions').mkdir();(root/'Sessions/Demo').write_text('CloseOnExit\\0\\\nWarnOnClose\\1\\\n'+('Fullscreen\\1\\\n' if '--fullscreen' in sys.argv else ''))
 key_args=[]
 if client_key is not None:
  ppk=root/'synthetic.ppk';ppk.write_bytes(PPK)
  key_args=['-i',str(ppk)]
 def launch():
  p=subprocess.Popen([str(binary),'--portable-dir',str(root),'--language',('ru' if '--russian' in sys.argv else 'en'),* ([] if '--unsaved' in sys.argv else ['-load','Demo']),'-ssh','127.0.0.1','-P',str(listener.getsockname()[1]),* ([] if '--unsaved' in sys.argv else ['-l','demo']),'-hostkey',fingerprint,*key_args]);processes.append(p);pids.add(p.pid)
  page=wait(lambda:next((h for h in enum() if pid(h)==p.pid and cls(h).startswith('KiTTY-ng') and u.IsWindowVisible(h)),None))
  time.sleep(1.5)
  if '--unsaved' in sys.argv:
   for letter in 'demo\r':u.PostMessageW(page,0x102,ord(letter),0)
   time.sleep(.5)
  for letter in (PASSPHRASE if client_key is not None else 'synthetic')+'\r':u.PostMessageW(page,0x102,ord(letter),0)
  return p
 try:
  p=launch();first=wait(lambda:next((h for h in enum() if pid(h)==p.pid and title(h)=='Demo session 1'),None))
  from paramiko import win_pageant
  before_agent=win_pageant.can_talk_to_agent()
  def agent_key():
   agent=paramiko.Agent()
   try:return next((k for k in agent.get_keys() if k.asbytes()==client_key.asbytes()),None)
   finally:agent.close()
  assert agent_key() is None, 'Synthetic key already exists; do not alter it'
  u.PostMessageW(first,0x112,0x270,0)
  wait(lambda:agent_key() is not None)
  agent=paramiko.Agent()
  try:
   signing=next(k for k in agent.get_keys() if k.asbytes()==client_key.asbytes())
   signature=signing.sign_ssh_data(b'kitty-ng-companion-regression')
   assert client_key.verify_ssh_sig(b'kitty-ng-companion-regression',paramiko.Message(signature))
  finally:agent.close()
  winscp=Path(os.environ['ProgramFiles(x86)'])/'WinSCP/WinSCP.com'
  assert winscp.is_file()
  before=len(authenticated)
  # This isolated loopback server has no SFTP service: prove authentication
  # separately from file transfer, without storing a test host key globally.
  command=f'open sftp://demo@127.0.0.1:{listener.getsockname()[1]}/ -hostkey=* -privatekey="{ppk}" -timeout=5'
  result=subprocess.run([str(winscp),'/ini=nul','/command',command,'exit'],capture_output=True,timeout=25)
  assert len(authenticated)>before, 'WinSCP did not authenticate using the unlocked agent key'
  assert PASSPHRASE.encode() not in result.stdout+result.stderr
  print('PASS: unsaved-session key unlock, Kageant signature, installed WinSCP agent authentication; no secret argv/file',flush=True)
 finally:
  if 'before_agent' in locals():
   from paramiko import win_pageant
   blob=client_key.asbytes();request=b'\x12'+struct.pack('>I',len(blob))+blob
   response=win_pageant._query_pageant(struct.pack('>I',len(request))+request)
   assert response and response[4:5]==b'\x06', 'Failed to remove synthetic key'
  for process_id in pids:
   subprocess.run(['taskkill','/PID',str(process_id),'/T','/F'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
  done.set()
  for t in transports:t.close()
  listener.close()
