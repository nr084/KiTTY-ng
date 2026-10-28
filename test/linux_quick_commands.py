"""Exercise shipped GTK quick-command menus against an isolated SSH server."""
import base64,hashlib,logging,os,socket,subprocess,sys,tempfile,threading,time
from pathlib import Path
import paramiko,pyatspi
logging.getLogger('paramiko').setLevel(logging.CRITICAL)
def wait(fn):
    end=time.monotonic()+15
    while time.monotonic()<end:
        try:value=fn()
        except subprocess.CalledProcessError:value=None
        if value:return value
        time.sleep(.1)
    raise AssertionError('Timeout waiting for quick-command GUI')
def nodes(n):
    yield n
    for i in range(n.childCount):
        try:yield from nodes(n.getChildAtIndex(i))
        except Exception:pass
root=pyatspi.Registry.getDesktop(0)
def find(name):return next((n for n in nodes(root) if n.name==name and n.getState().contains(pyatspi.STATE_SHOWING)),None)
def click(n):
    r=n.queryComponent().getExtents(pyatspi.DESKTOP_COORDS)
    subprocess.run(['xdotool','mousemove','--sync',str(r.x+r.width//2),str(r.y+r.height//2),'click','1'],check=True)
key=paramiko.RSAKey.generate(2048)
fp='SHA256:'+base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip('=')
listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(15)
received=bytearray();ready=threading.Event();transport=None
class Server(paramiko.ServerInterface):
    def check_auth_password(self,user,password):return paramiko.AUTH_SUCCESSFUL if (user,password)==('demo','synthetic') else paramiko.AUTH_FAILED
    def get_allowed_auths(self,user):return 'password'
    def check_channel_request(self,*args):return paramiko.OPEN_SUCCEEDED
    def check_channel_pty_request(self,*args):return True
    def check_channel_shell_request(self,*args):ready.set();return True
def serve():
    global transport
    sock,_=listener.accept();transport=paramiko.Transport(sock);transport.add_server_key(key);transport.start_server(server=Server());channel=transport.accept(15)
    channel.send(b'\x1b]0;Quick command test\x07\r\n$ ')
    try:
        while True:
            data=channel.recv(4096)
            if not data:break
            received.extend(data)
    except (OSError,EOFError):pass
threading.Thread(target=serve,daemon=True).start()
with tempfile.TemporaryDirectory(prefix='kitty-quick-') as d:
    folder=Path(d);(folder/'Sessions').mkdir()
    ini=folder/'kitty.ini';ini.write_text('[KiTTY]\nunknown=keep\n[QuickCommands]\ncmd1_name=Тест & один\ncmd1_instruction=echo Привет\ncmd1_enter=no\ncmd2_name=Second command\ncmd2_instruction=echo second\ncmd2_enter=yes\n',encoding='utf16')
    env={**os.environ,'KITTY_NG_PORTABLE_DIR':d,'KITTY_NG_LANGUAGE':'en','G_DEBUG':'fatal-criticals','NO_AT_BRIDGE':'0'}
    p=subprocess.Popen([sys.argv[1],'-ssh','127.0.0.1','-P',str(listener.getsockname()[1]),'-l','demo','-pw','synthetic','-hostkey',fp],env=env)
    try:
        assert ready.wait(15)
        wid=wait(lambda:subprocess.check_output(['xdotool','search','--name','Quick command test'],text=True).split()[-1])
        def menu():
            subprocess.run(['xdotool','mousemove','--window',wid,'70','100','click','3'],check=True)
            click(wait(lambda:find('Quick commands')))
        menu();click(wait(lambda:find('Тест & один')))
        expected='echo Привет'.encode();wait(lambda:len(received)>=len(expected));assert bytes(received)==expected,received
        menu();click(wait(lambda:find('Second command')))
        expected+='echo second\r'.encode();wait(lambda:len(received)>=len(expected));assert bytes(received)==expected,received
        assert 'unknown=keep' in ini.read_text(encoding='utf16')
        print('PASS: native GTK quick commands: UTF-8 names, literal text, optional Enter, existing INI preserved')
    finally:
        p.terminate();p.wait(timeout=10)
        if transport:transport.close()
        listener.close()
