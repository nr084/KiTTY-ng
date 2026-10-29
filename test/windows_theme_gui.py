"""Rendered dark/light dialogs and Kageant translated button geometry."""
import ctypes,os,subprocess,sys,tempfile,time
from ctypes import wintypes as w
from pathlib import Path
from PIL import Image
build=Path(sys.argv[1]).resolve(); arch=sys.argv[2] if len(sys.argv)>2 else 'x64'
u=ctypes.WinDLL('user32');g=ctypes.WinDLL('gdi32')
u.GetDC.argtypes=[w.HWND];u.GetDC.restype=w.HDC
u.ReleaseDC.argtypes=[w.HWND,w.HDC]
u.SendMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM];u.SendMessageW.restype=ctypes.c_ssize_t
g.SelectObject.argtypes=[w.HDC,w.HANDLE];g.SelectObject.restype=w.HANDLE
g.GetTextExtentPoint32W.argtypes=[w.HDC,w.LPCWSTR,ctypes.c_int,ctypes.POINTER(w.SIZE)]
u.GetWindowRect.argtypes=[w.HWND,ctypes.POINTER(w.RECT)]
u.GetWindowTextW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int]
u.GetClassNameW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int]
callback=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
def check_agent(pid):
 found=[]
 @callback
 def top(hwnd,unused):
  p=w.DWORD();u.GetWindowThreadProcessId(hwnd,ctypes.byref(p))
  if p.value==pid and u.IsWindowVisible(hwnd):found.append(hwnd)
  return True
 deadline=time.monotonic()+5
 while not found and time.monotonic()<deadline:u.EnumWindows(top,0);time.sleep(.01)
 assert found,'Kageant dialog missing'
 buttons=[]
 @callback
 def child(hwnd,unused):
  name=ctypes.create_unicode_buffer(128);u.GetClassNameW(hwnd,name,128)
  if name.value.lower()=='button' and u.IsWindowVisible(hwnd):buttons.append(hwnd)
  return True
 u.EnumChildWindows(found[0],child,0)
 assert len(buttons)>=5,buttons
 for hwnd in buttons:
  text=ctypes.create_unicode_buffer(256);u.GetWindowTextW(hwnd,text,256)
  value=text.value.replace('&','');r=w.RECT();u.GetWindowRect(hwnd,ctypes.byref(r))
  dc=u.GetDC(hwnd);old=g.SelectObject(dc,u.SendMessageW(hwnd,0x31,0,0));size=w.SIZE()
  g.GetTextExtentPoint32W(dc,value,len(value),ctypes.byref(size));g.SelectObject(dc,old);u.ReleaseDC(hwnd,dc)
  assert size.cx+8<=r.right-r.left,(value,size.cx,r.right-r.left)
with tempfile.TemporaryDirectory(prefix='kitty-ng-theme-gui-') as temp:
 root=Path(temp);env=dict(os.environ,KITTY_NG_PORTABLE_DIR=str(root))
 for mode in (1,2):
  (root/'kitty.ini').write_text('[KiTTY]\nUITheme='+str(mode)+'\n',encoding='utf-8')
  for lang in ('ru','en'):
   for tool in ('kitty-ng','kittygen','kageant','session','about',
                'Session/Logging','Terminal/Bell','Window/Translation','Window/Colours',
                'Connection/SSH/Kex','Connection/SSH/Tunnels','Connection/Automation','Connection/Proxy profiles'):
    image=root/(tool.replace('/','-')+lang+str(mode)+'.png')
    args=['--language',lang]
    if tool=='about':args+=['--portable-dir',str(root),'-demo-about',str(image)]
    elif '/' in tool:args+=['--portable-dir',str(root),'-demo-config-box',str(image),'-demo-panel',tool]
    elif tool=='session':args+=['--portable-dir',str(root),'-demo-config-box',str(image),'-demo-panel','Session']
    elif tool=='kitty-ng':args+=['--portable-dir',str(root),'-demo-config-box',str(image),'-demo-panel','Window/Appearance']
    else:args+=['-demo-screenshot',str(image)]
    process=subprocess.Popen([str(build/(('kitty-ng' if tool in ('session','about') or '/' in tool else tool)+'-'+arch+'.exe')),*args],env=env)
    try:
     if tool=='kageant':check_agent(process.pid)
     code=process.wait(timeout=20)
     assert code==0,(tool,arch,lang,mode,code)
    finally:
     if process.poll() is None:process.terminate();process.wait()
    with Image.open(image) as source:
     pixels=list(source.convert('RGB').getdata());ratio=sum(max(p)<85 for p in pixels)/len(pixels)
     assert (ratio>.5 if mode==2 else ratio<.4),(tool,lang,mode,ratio)
print('PASS: 52 rendered Windows/client/utility dialogs, light/dark RU/EN, full Kageant labels')
