"""Native Linux tab lifecycle, input, reorder and close checks (runtime only)."""
import base64,hashlib,logging,os,socket,subprocess,sys,tempfile,threading,time
from pathlib import Path
import paramiko,pyatspi
logging.getLogger('paramiko').setLevel(logging.CRITICAL)
def wait(fn,seconds=15):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  try:
   value=fn()
   if value:return value
  except Exception:pass
  time.sleep(.1)
 raise AssertionError('Timed out: '+str(fn))
def nodes(item):
 yield item
 try:
  for child in item:
   if child:yield from nodes(child)
 except Exception:pass
def find(name):return next((n for n in nodes(pyatspi.Registry.getDesktop(0)) if n.name==name and n.getState().contains(pyatspi.STATE_SHOWING)),None)
def click(item,button=1):
 rect=item.queryComponent().getExtents(pyatspi.DESKTOP_COORDS)
 subprocess.run(['xdotool','mousemove','--sync',str(rect.x+rect.width//2),str(rect.y+rect.height//2),'click',str(button)],check=True)
def tabs():return [n for n in nodes(pyatspi.Registry.getDesktop(0)) if n.getRoleName()=='page tab' and n.getState().contains(pyatspi.STATE_SHOWING)]
key=paramiko.RSAKey.generate(2048);fingerprint='SHA256:'+base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip('=')
listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen();listener.settimeout(.2)
channels=[];transports=[];received={};done=threading.Event()
from duplicate_auth_fixture import PPK,PASSPHRASE,PUBLIC
client_key=paramiko.Ed25519Key(data=base64.b64decode(PUBLIC)) if "--key" in sys.argv else None
class Server(paramiko.ServerInterface):
 def check_auth_none(self,user):return paramiko.AUTH_FAILED
 def check_auth_password(self,user,password):return paramiko.AUTH_SUCCESSFUL if (user,password)==('demo','synthetic') else paramiko.AUTH_FAILED
 def check_auth_publickey(self,user,key):return paramiko.AUTH_SUCCESSFUL if user=='demo' and client_key is not None and key==client_key else paramiko.AUTH_FAILED
 def get_allowed_auths(self,user):return 'publickey' if client_key is not None else 'password'
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

binary=Path(sys.argv[1]).resolve();output=Path(sys.argv[2]);output.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='kitty-tabs-') as folder:
 root=Path(folder);(root/'Sessions').mkdir();(root/'Sessions/Demo').write_text('WarnOnClose\\1\\\n')
 env={**os.environ,'KITTY_NG_PORTABLE_DIR':str(root),'KITTY_NG_LANGUAGE':'en','G_DEBUG':'fatal-criticals','NO_AT_BRIDGE':'0'}
 key_args=[]
 if client_key is not None:
  ppk=root/'synthetic.ppk';ppk.write_bytes(PPK);key_args=['-i',str(ppk)]
 log=(output/'linux-tabs.log').open('w')
 process=subprocess.Popen([str(binary),'-ssh','127.0.0.1','-P',str(listener.getsockname()[1]),'-hostkey',fingerprint,*key_args],env=env,stdout=log,stderr=log)
 try:
  time.sleep(1)
  initial=wait(lambda:subprocess.check_output(["xdotool","search","--pid",str(process.pid)],text=True).split()[-1])
  subprocess.run(["xdotool","windowfocus",initial,"type","demo"],check=True)
  subprocess.run(["xdotool","key","Return"],check=True);time.sleep(.5)
  subprocess.run(["xdotool","windowfocus",initial,"type",PASSPHRASE if client_key is not None else "synthetic"],check=True)
  subprocess.run(["xdotool","key","Return"],check=True)
  wait(lambda:len(channels)==1)
  assert all(t.queryComponent().getExtents(pyatspi.DESKTOP_COORDS).width<0 for t in tabs())
  wid=wait(lambda:subprocess.check_output(['xdotool','search','--name','Demo session 1'],text=True).split()[-1])
  subprocess.run(['xdotool','mousemove','--window',wid,'70','100','click','3'],check=True)
  menu=wait(lambda:find('Duplicate Session') or find('Duplicate session'))
  click(menu)
  wait(lambda:len(channels)==2)
  wait(lambda:len(tabs())==2)
  print('TABS',[(t.name,t.getRoleName()) for t in tabs()],flush=True)
  first,second=tabs()
  def drag(source,target):
   a=source.queryComponent().getExtents(pyatspi.DESKTOP_COORDS);b=target.queryComponent().getExtents(pyatspi.DESKTOP_COORDS)
   x=a.x+30;y=a.y+a.height//2;end=b.x+b.width//2
   subprocess.run(['xdotool','mousemove','--sync',str(x),str(y),'mousedown','1'],check=True)
   for step in range(1,13):
    subprocess.run(['xdotool','mousemove','--sync',str(round(x+(end-x)*step/12)),str(y)],check=True);time.sleep(.03)
   subprocess.run(['xdotool','mouseup','1'],check=True)
  drag(first,second);wait(lambda:tabs()[1].name==first.name)
  drag(tabs()[1],tabs()[0]);wait(lambda:tabs()[0].name==first.name)
  click(tabs()[1]);time.sleep(.4)
  import gi
  gi.require_version('Gdk','3.0');from gi.repository import Gdk
  geometry=dict(line.split('=',1) for line in subprocess.check_output(['xdotool','getwindowgeometry','--shell',wid],text=True).splitlines() if '=' in line)
  picture=Gdk.pixbuf_get_from_window(Gdk.get_default_root_window(),int(geometry['X']),int(geometry['Y']),int(geometry['WIDTH']),int(geometry['HEIGHT']))
  picture.savev(str(output/'linux-tabs-en.png'),'png',[],[])
  from PIL import Image
  shot=Image.open(output/'linux-tabs-en.png').convert('RGB')
  assert sum(max(pixel)<30 for pixel in shot.getdata())>shot.width*shot.height//2, 'Terminal backing surface must be drawn, not blank'

  click(first,3);click(wait(lambda:find('Duplicate session')))
  wait(lambda:len(channels)==3 and len(tabs())==3)
  click(tabs()[2],3);click(wait(lambda:find('Close tab')))
  wait(lambda:len(tabs())==2)
  click(first,3);click(wait(lambda:find('Move right')))
  wait(lambda:tabs()[1].name==first.name)
  assert len(channels)==3
  # Closing a tab through the menu must bypass WarnOnClose=1.
  click(tabs()[0],3);click(wait(lambda:find('Close tab')))
  wait(lambda:len(tabs())==1)
  assert process.poll() is None
  assert not find('KiTTY-ng Exit Confirmation')
  subprocess.run(['xdotool','windowfocus',wid,'type','A'],check=True)
  wait(lambda:received[1]==b'A')
  from Xlib import X,display,protocol
  display_connection=display.Display();window=display_connection.create_resource_object('window',int(wid))
  event=protocol.event.ClientMessage(window=window,client_type=display_connection.intern_atom('WM_PROTOCOLS'),data=(32,[display_connection.intern_atom('WM_DELETE_WINDOW'),X.CurrentTime,0,0,0]))
  window.send_event(event,event_mask=0);display_connection.flush();display_connection.close()
  wait(lambda:find('KiTTY-ng Exit Confirmation'))
  click(wait(lambda:find('No')))
  assert process.poll() is None
  print('PASS: Linux independent SSH tabs, hidden single tab bar, mouse drag and menu reorder without reconnect, prompt-free tab close, surviving input, window confirmation',flush=True)
 finally:
  if process.poll() is None:process.terminate()
  process.wait(10);log.close();done.set()
  for t in transports:t.close()
  listener.close()
