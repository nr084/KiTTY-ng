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

u.SendMessageW=ctypes.WINFUNCTYPE(ctypes.c_ssize_t,w.HWND,w.UINT,w.WPARAM,w.LPARAM)(('SendMessageW',u))
def edit_text(h):
 buffer=ctypes.create_unicode_buffer(512);u.SendMessageW(h,0xD,512,ctypes.addressof(buffer));return buffer.value
with tempfile.TemporaryDirectory(prefix='kitty-ng-folder-ui-') as directory:
 root=Path(directory);folder=root/'Sessions/AUTOBUD';folder.mkdir(parents=True)
 profile=folder/'BROOMER%20HOSTING';profile.write_text('HostName\\example.invalid\\\nProtocol\\ssh\\\n')
 p=subprocess.Popen([str(Path(sys.argv[1]).resolve()),'--portable-dir',directory,'--language','en'],env=dict(os.environ,KITTY_NG_PORTABLE_DIR=directory))
 try:
  dialog=wait(lambda:next((h for h in windows() if pid(h)==p.pid and cls(h)=='PuTTYConfigBox'),None))
  wait(lambda:u.IsWindowVisible(dialog))
  def folder_entry():
   for combo in children(dialog):
    if cls(combo).lower()!='combobox':continue
    count=ctypes.c_int32(u.SendMessageW(combo,0x146,0,0)).value
    for index in range(count):
     buffer=ctypes.create_unicode_buffer(512)
     u.SendMessageW(combo,0x148,index,ctypes.addressof(buffer))
     if buffer.value=='AUTOBUD':return combo,index
  combo,index=wait(folder_entry)
  u.SendMessageW(combo,0x14E,index,0);u.SendMessageW(dialog,0x111,u.GetDlgCtrlID(combo)|(1<<16),combo)
  def selected_profile_row():
   for listing in children(dialog):
    if cls(listing).lower()!='listbox':continue
    count=u.SendMessageW(listing,0x18B,0,0)
    for row in range(count):
     buffer=ctypes.create_unicode_buffer(512)
     u.SendMessageW(listing,0x189,row,ctypes.addressof(buffer))
     if buffer.value=='BROOMER HOSTING':return listing,row
  listing,index=wait(selected_profile_row)
  u.SendMessageW(listing,0x186,index,0);u.SendMessageW(dialog,0x111,u.GetDlgCtrlID(listing)|(1<<16),listing)
  load=next(h for h in children(dialog) if cls(h)=='Button' and title(h).replace('&','')=='Load')
  u.SendMessageW(dialog,0x111,u.GetDlgCtrlID(load),load)
  assert any(edit_text(h)=='BROOMER HOSTING' for h in children(dialog) if cls(h).lower()=='edit'),[(cls(h),title(h)) for h in children(dialog)]
  save=next(h for h in children(dialog) if cls(h)=='Button' and title(h).replace('&','')=='Save')
  u.SendMessageW(dialog,0x111,u.GetDlgCtrlID(save),save)
  assert profile.exists() and not (folder/'AUTOBUD').exists()
  print('PASS: selected folder omitted from editable session name; saving preserves canonical profile path')
 finally:
  p.kill();p.wait()
