"""Native Windows/GTK regression: runtime and profile passwords share one attempt.

Only disposable loopback credentials are used. Reports contain counters, not secrets.
"""
import argparse,base64,hashlib,json,logging,os,socket,subprocess,tempfile,threading,time
from pathlib import Path
import paramiko
logging.getLogger('paramiko').setLevel(logging.CRITICAL)
parser=argparse.ArgumentParser();parser.add_argument('binary',type=Path);parser.add_argument('--report',type=Path,required=True);args=parser.parse_args()
binary=args.binary.resolve();args.report.parent.mkdir(parents=True,exist_ok=True)
vector=json.loads(Path(__file__).with_name('kittyng-password-vector.json').read_text())
initial=vector['synthetic_password'];rotated=initial+'-rotated'
state={'rotated':False,'attempts':{},'channels':{},'received':{}}
stop=threading.Event();transports=[];process=None;pids=set()
payload = binary.parent/'usr/bin/kitty-ng' if binary.name=='AppRun' else binary
report={'status':'running','platform':os.name,'binary_sha256':hashlib.sha256(payload.read_bytes()).hexdigest()}
def wait(fn):
 end=time.monotonic()+20
 while time.monotonic()<end:
  result=fn()
  if result:return result
  time.sleep(.05)
 raise AssertionError('Timed out waiting for native auth regression')
hostkey=paramiko.RSAKey.generate(2048)
fingerprint='SHA256:'+base64.b64encode(hashlib.sha256(hostkey.asbytes()).digest()).decode().rstrip('=')
listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen();listener.settimeout(.2)
class Server(paramiko.ServerInterface):
 def __init__(self,index):self.index=index
 def get_allowed_auths(self,user):return 'password'
 def check_auth_password(self,user,value):
  ok=user=='root' and value==(rotated if state['rotated'] else initial)
  state['attempts'].setdefault(self.index,[]).append(ok)
  return paramiko.AUTH_SUCCESSFUL if ok else paramiko.AUTH_FAILED
 def check_channel_request(self,*a):return paramiko.OPEN_SUCCEEDED
 def check_channel_pty_request(self,*a):return True
 def check_channel_window_change_request(self,*a):return True
 def check_channel_shell_request(self,*a):return True
def connection(sock,index):
 try:
  t=paramiko.Transport(sock);transports.append(t);t.add_server_key(hostkey);t.start_server(server=Server(index));channel=t.accept(30)
  if channel is None:return
  state['channels'][index]=channel;state['received'][index]=bytearray()
  channel.sendall(f'\x1b]0;Retry session {index}\x07\r\nSynthetic password retry test\r\n$ '.encode())
  while not stop.is_set():
   data=channel.recv(4096)
   if not data:break
   state['received'][index].extend(data)
 except (OSError,EOFError,paramiko.SSHException):pass
def accept():
 index=0
 while not stop.is_set():
  try:sock,_=listener.accept()
  except socket.timeout:continue
  except OSError:return
  index+=1;threading.Thread(target=connection,args=(sock,index),daemon=True).start()
threading.Thread(target=accept,daemon=True).start()
if os.name=='nt':
 import ctypes
 from ctypes import wintypes as w
 u=ctypes.WinDLL('user32');callback=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
 for name in ('PostMessageW','SendMessageW'):
  fn=getattr(u,name);fn.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM];fn.restype=ctypes.c_ssize_t
 u.GetParent.argtypes=[w.HWND];u.GetParent.restype=w.HWND
 def windows(parent=None):
  result=[]
  @callback
  def each(h,_):result.append(h);return True
  if parent:u.EnumChildWindows(parent,each,0)
  else:u.EnumWindows(each,0)
  return result
 def title(h):
  b=ctypes.create_unicode_buffer(512);u.GetWindowTextW(h,b,512);return b.value
 def cls(h):
  b=ctypes.create_unicode_buffer(256);u.GetClassNameW(h,b,256);return b.value
 def pid(h):
  value=w.DWORD();u.GetWindowThreadProcessId(h,ctypes.byref(value));return value.value
 def type_text(h,text):
  for c in text:u.PostMessageW(h,0x102,ord(c),0)
else:
 import pyatspi
 def nodes(item):
  yield item
  try:
   for child in item:
    if child:yield from nodes(child)
  except Exception:pass
 def find(name):return next((n for n in nodes(pyatspi.Registry.getDesktop(0)) if n.name==name and n.getState().contains(pyatspi.STATE_SHOWING)),None)
 def click(item,button=1):
  r=item.queryComponent().getExtents(pyatspi.DESKTOP_COORDS)
  subprocess.run(['xdotool','mousemove','--sync',str(r.x+r.width//2),str(r.y+r.height//2),'click',str(button)],check=True)
with tempfile.TemporaryDirectory(prefix='kitty-retry-') as folder:
 root=Path(folder);(root/'Sessions').mkdir()
 (root/'Sessions/Demo').write_text('HostName\\127.0.0.1\\\nUserName\\root\\\nTermType\\xterm\\\nPassword\\'+vector['encoded']+'\\\nWarnOnClose\\0\\\n',encoding='utf8')
 env={**os.environ,'KITTY_NG_PORTABLE_DIR':str(root),'KITTY_NG_LANGUAGE':'en','NO_AT_BRIDGE':'0'}
 command=[str(binary),'-load','Demo','-ssh','127.0.0.1','-P',str(listener.getsockname()[1]),'-l','root','-hostkey',fingerprint]
 if os.name=='nt':command[1:1]=['--portable-dir',str(root),'--language','en']
 try:
  process=subprocess.Popen(command,env=env);pids.add(process.pid)
  wait(lambda:1 in state['channels'])
  assert state['attempts'][1]==[True], 'Saved-profile login failed'
  state['rotated']=True
  if os.name=='nt':
   first=wait(lambda:next((h for h in windows() if pid(h)==process.pid and title(h)=='Retry session 1'),None))
   u.PostMessageW(first,0x112,0x30,0)
  else:
   wid=wait(lambda:subprocess.check_output(['xdotool','search','--name','Retry session 1'],text=True).split()[-1])
   subprocess.run(['xdotool','mousemove','--window',wid,'70','100','click','3'],check=True)
   click(wait(lambda:find('Duplicate Session') or find('Duplicate session')))
  wait(lambda:state['attempts'].get(2));time.sleep(1)
  report['automatic_rejected_attempts']=len(state['attempts'][2])
  assert state['attempts'][2]==[False], 'Runtime cache was followed by an extra saved-password attempt'
  if os.name=='nt':
   host=wait(lambda:u.GetParent(first));pids.add(pid(host))
   second=wait(lambda:next((h for h in windows(host) if h!=first and cls(h)=='KiTTY-ng' and u.IsWindowVisible(h)),None));pids.add(pid(second))
   type_text(second,rotated+'\r')
  else:
   subprocess.run(['xdotool','windowfocus',wid,'type',rotated],check=True);subprocess.run(['xdotool','key','Return'],check=True)
  wait(lambda:2 in state['channels']);assert state['attempts'][2]==[False,True]
  if os.name=='nt':u.PostMessageW(second,0x112,0x30,0)
  else:
   click(wait(lambda:next((n for n in nodes(pyatspi.Registry.getDesktop(0)) if n.getRoleName()=='page tab' and n.name=='Retry session 2'),None)),3);click(wait(lambda:find('Duplicate session')))
  wait(lambda:3 in state['channels']);assert state['attempts'][3]==[True]
  report['manual_recovery']=True;report['updated_cache_reused']=True;report['status']='passed'
  print('PASS: one automatic rejection, manual recovery and updated cache in the next duplicate',flush=True)
 except BaseException as error:
  report['status']='failed';report['error']=str(error);raise
 finally:
  report['auth_events']={str(k):v for k,v in state['attempts'].items()}
  args.report.write_text(json.dumps(report,indent=2)+'\n')
  if os.name=='nt':
   for h in windows():
    if cls(h)=='KiTTYng.SessionTabs' and any(pid(c) in pids for c in windows(h)):
     pids.add(pid(h));pids.update(pid(c) for c in windows(h))
   for p in pids:subprocess.run(['taskkill','/PID',str(p),'/T','/F'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
  elif process and process.poll() is None:process.terminate();process.wait(10)
  stop.set();listener.close()
  for t in transports:t.close()
