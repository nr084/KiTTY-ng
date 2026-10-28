"""Inspect the real Change Settings dialog of an already initialized SSH session."""
import ctypes,time
from ctypes import wintypes as w

def verify(process, terminal, u, windows, title, until, english=False):
    k=ctypes.WinDLL('kernel32',use_last_error=True)
    k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
    k.VirtualAllocEx.argtypes=[w.HANDLE,ctypes.c_void_p,ctypes.c_size_t,w.DWORD,w.DWORD];k.VirtualAllocEx.restype=ctypes.c_void_p
    k.WriteProcessMemory.argtypes=[w.HANDLE,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p]
    k.ReadProcessMemory.argtypes=k.WriteProcessMemory.argtypes
    k.VirtualFreeEx.argtypes=[w.HANDLE,ctypes.c_void_p,ctypes.c_size_t,w.DWORD]
    k.CloseHandle.argtypes=[w.HANDLE]
    u.GetClassNameW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int]
    u.GetWindowRect.argtypes=[w.HWND,ctypes.POINTER(w.RECT)]
    u.GetClientRect.argtypes=[w.HWND,ctypes.POINTER(w.RECT)]
    u.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
    callback=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
    def children(hwnd):
        result=[]
        @callback
        def each(h,p):
            result.append(h);return True
        u.EnumChildWindows(hwnd,each,0);return result
    def cls(h):
        b=ctypes.create_unicode_buffer(128);u.GetClassNameW(h,b,128);return b.value
    u.PostMessageW(terminal,0x112,0x50,0)
    dialog=until(lambda:next((h for h in windows(process.pid) if h!=terminal and any(cls(c)=='SysTreeView32' for c in children(h))),None))
    tree=next(c for c in children(dialog) if cls(c)=='SysTreeView32')
    handle=k.OpenProcess(0x438,False,process.pid);assert handle
    wow=w.BOOL();k.IsWow64Process(handle,ctypes.byref(wow));pointer=ctypes.c_uint32 if wow.value else ctypes.c_uint64
    class Item(ctypes.Structure):
        _fields_=[('mask',w.UINT),('item',pointer),('state',w.UINT),('stateMask',w.UINT),('text',pointer),('length',ctypes.c_int),('image',ctypes.c_int),('selectedImage',ctypes.c_int),('children',ctypes.c_int),('param',pointer)]
    memory=k.VirtualAllocEx(handle,None,8192,0x3000,4);assert memory
    def label(item):
        data=Item();data.mask=1;data.item=item;data.text=memory+1024;data.length=1024
        assert k.WriteProcessMemory(handle,memory,ctypes.byref(data),ctypes.sizeof(data),None)
        assert u.SendMessageW(tree,0x113e,0,memory)
        text=ctypes.create_unicode_buffer(1024)
        assert k.ReadProcessMemory(handle,memory+1024,text,ctypes.sizeof(text),None)
        return text.value
    def walk(item):
        while item:
            yield item,label(item)
            yield from walk(u.SendMessageW(tree,0x110a,4,item))
            item=u.SendMessageW(tree,0x110a,1,item)
    wanted={'Automation','Proxy profiles','Tunnels','Kex','Colours','Translation','Bell','Copy','Selection','Quick commands'} if english else {'Автоматизация','Профили прокси','Туннели','Обмен ключами','Цвета','Кодировка','Сигнал','Копировать','Выделение','Быстрые команды'}
    checked=[]
    try:
        nodes=list(walk(u.SendMessageW(tree,0x110a,0,0)))
        for item,name in nodes:
            if name not in wanted:continue
            assert u.SendMessageW(tree,0x110b,9,item)
            time.sleep(.08)
            text_controls=[c for c in children(dialog) if cls(c).lower()=='static' and u.IsWindowVisible(c)]
            texts=[title(c) for c in text_controls]
            assert all('\ufffd' not in text and text.count('\n')<8 for text in texts),(name,texts)
            client=w.RECT();u.GetClientRect(dialog,ctypes.byref(client))
            origin=w.POINT();u.ClientToScreen(dialog,ctypes.byref(origin))
            for c in text_controls:
                if not title(c):continue
                rect=w.RECT();u.GetWindowRect(c,ctypes.byref(rect))
                assert rect.bottom<=origin.y+client.bottom,(name,title(c),rect.bottom,origin.y+client.bottom)
            if name in ('Selection','Выделение'):
                options=[]
                for c in children(dialog):
                    if cls(c).lower()!='combobox':continue
                    for index in range(u.SendMessageW(c,0x146,0,0)):
                        text=ctypes.create_unicode_buffer(1024);u.SendMessageW(c,0x148,index,ctypes.addressof(text));options.append(text.value)
                for expected in (('System clipboard','Last selected text') if english else ('Системный буфер обмена','Последний выделенный текст')):assert expected in options,(expected,options)
            if name in ('Quick commands','Быстрые команды'):
                visible=[c for c in children(dialog) if u.IsWindowVisible(c)]
                listing=next(c for c in visible if cls(c).lower()=='listbox')
                edits=[c for c in visible if cls(c).lower()=='edit']
                assert len(edits)==2,(name,edits)
                buttons={title(c).replace('&',''):c for c in visible if cls(c).lower()=='button'}
                count=u.SendMessageW(listing,0x18b,0,0)
                initial=ctypes.create_unicode_buffer(256)
                u.SendMessageW(listing,0x189,0,ctypes.addressof(initial))
                assert initial.value==('Update Ubuntu' if english else 'Обновить Ubuntu'),initial.value
                u.SendMessageW(buttons['Add' if english else 'Добавить'],0xf5,0,0)
                for edit,text in zip(edits,['Delete','echo Проверка && echo 2']):
                    buffer=ctypes.create_unicode_buffer(text)
                    u.SendMessageW(edit,0xc,0,ctypes.addressof(buffer))
                u.SendMessageW(buttons['Save' if english else 'Сохранить'],0xf5,0,0)
                assert u.SendMessageW(listing,0x18b,0,0)==count+1
                row=u.SendMessageW(listing,0x188,0,0);buf=ctypes.create_unicode_buffer(256)
                u.SendMessageW(listing,0x189,row,ctypes.addressof(buf));assert buf.value=='Delete',buf.value
                buf=ctypes.create_unicode_buffer(1024)
                u.SendMessageW(edits[1],0xd,len(buf),ctypes.addressof(buf))
                assert buf.value=='echo Проверка && echo 2',buf.value
                u.SendMessageW(buttons['Delete' if english else 'Удалить'],0xf5,0,0)
                assert u.SendMessageW(listing,0x18b,0,0)==count
            checked.append(name)
        assert set(checked)==wanted,(checked,nodes)
        print('PASS: live SSH Change Settings: '+', '.join(checked),flush=True)
    finally:
        k.VirtualFreeEx(handle,memory,0,0x8000);k.CloseHandle(handle)
        u.SendMessageW(dialog,0x111,2,0)
        until(lambda:not u.IsWindow(dialog))
