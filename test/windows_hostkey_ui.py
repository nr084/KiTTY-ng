"""Native SSH trust prompt layout and unchanged Accept/Once/Cancel semantics."""
import ctypes,os,sys,time,socket,threading,tempfile,subprocess,logging,json
from pathlib import Path
from ctypes import wintypes as w
import paramiko
logging.getLogger('paramiko').setLevel(logging.CRITICAL)
u=ctypes.WinDLL('user32');u.SetProcessDPIAware();u.SendMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM];u.SendMessageW.restype=ctypes.c_ssize_t;u.GetDlgItem.argtypes=[w.HWND,ctypes.c_int];u.GetDlgItem.restype=w.HWND
u.GetWindowRect.argtypes=[w.HWND,ctypes.POINTER(w.RECT)];u.GetWindowTextW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int]
callback=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
def text(h):
 b=ctypes.create_unicode_buffer(8192);u.SendMessageW(h,0xD,len(b),ctypes.addressof(b));return b.value
def rect(h):
 r=w.RECT();u.GetWindowRect(h,ctypes.byref(r));return r
def wait(fn):
 end=time.monotonic()+12
 while time.monotonic()<end:
  r=fn()
  if r:return r
  time.sleep(.05)
 raise AssertionError('Host key fixture timeout')
def prompt(pid):
 found=[]
 @callback
 def each(h,_):
  n=w.DWORD();u.GetWindowThreadProcessId(h,ctypes.byref(n));b=ctypes.create_unicode_buffer(100);u.GetClassNameW(h,b,100)
  if n.value==pid and b.value=='PuTTYHostKeyDialog' and u.IsWindowVisible(h):found.append(h)
  return True
 u.EnumWindows(each,0);return found[0] if found else None
key=paramiko.RSAKey.generate(2048);results=[]
for lang in ('en','ru'):
 for mode in (1,2):
  for action in (2,1000,1001):
   auth=threading.Event();listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen();listener.settimeout(15);transports=[]
   class Server(paramiko.ServerInterface):
    def check_auth_password(self,user,password):auth.set();return paramiko.AUTH_SUCCESSFUL
    def get_allowed_auths(self,user):return 'password'
    def check_channel_request(self,*args):return paramiko.OPEN_SUCCEEDED
    def check_channel_pty_request(self,*args):return True
    def check_channel_shell_request(self,*args):return True
   def serve():
    try:
     s,_=listener.accept();t=paramiko.Transport(s);transports.append(t);t.add_server_key(key);t.start_server(server=Server());t.accept(15)
    except Exception:pass
   threading.Thread(target=serve,daemon=True).start()
   with tempfile.TemporaryDirectory() as d:
    root=Path(d);(root/'kitty.ini').write_text('[KiTTY]\nUITheme='+str(mode)+'\n')
    p=subprocess.Popen([sys.argv[1],'--portable-dir',d,'--language',lang,'-ssh','127.0.0.1','-P',str(listener.getsockname()[1]),'-l','fixture','-pw','synthetic'])
    try:
     h=wait(lambda:prompt(p.pid));time.sleep(.25);bounds=rect(h);body=u.GetDlgItem(h,100);r=rect(body)
     assert r.right-r.left>400 and r.bottom-r.top>100 and 'SHA256:' in text(body),(r.left,r.right,r.top,r.bottom,text(body))
     buttons=[u.GetDlgItem(h,i) for i in (1004,1001,1000,2)];boxes=[rect(b) for b in buttons]
     assert all(b.bottom<bounds.bottom and b.top>=r.bottom for b in boxes)
     assert all(a.right<=b.left for a,b in zip(boxes,boxes[1:]))
     if action==2:
      from PIL import ImageGrab
      out=Path(__file__).resolve().parents[1]/'dist/ui-review';out.mkdir(exist_ok=True)
      u.SetForegroundWindow(h);time.sleep(.2);ImageGrab.grab(bbox=(bounds.left,bounds.top,bounds.right,bounds.bottom)).save(out/f'hostkey-{lang}-{mode}.png')
     u.SendMessageW(u.GetDlgItem(h,action),0xF5,0,0)
     if action!=2:assert auth.wait(8)
     else:time.sleep(.2);assert not auth.is_set()
     stored=list((root/'SshHostKeys').glob('*'))
     assert bool(stored)==(action==1001),(action,stored)
     results.append({'language':lang,'theme':mode,'action':action,'status':'passed'})
    finally:
     if p.poll() is None:p.terminate()
     p.wait(timeout=5)
     for t in transports:t.close()
     listener.close()
print('PASS: 12 host-key dialogs, RU/EN light/dark, geometry and trust decisions')
Path('dist/hostkey-ui.json').write_text(json.dumps({'status':'passed','tests':results},indent=2))
