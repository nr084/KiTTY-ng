"""Bound coverage reuse to app-only session-menu changes and added GUI strings."""
import hashlib,json,subprocess
from pathlib import Path

def proof(root,old,new):
    def git(*args): return subprocess.check_output(['git',*args],cwd=root).decode().replace('\r\n','\n')
    def read(commit,path): return git('show',commit+':'+path)
    try:
        changed=git('diff','--name-only',old,new).splitlines()
        sources={p for p in changed if Path(p).suffix in ('.c','.h','.m','.cmake','.pl') or Path(p).name in ('CMakeLists.txt','VERSION')}
        if sources!={'macos/client.m','macos/selftest-ui.m','locale/builtin.h'}: return None
        if any(p.startswith('test/') for p in changed): return None
        if read(old,'locale/ru.json')!=read(new,'locale/ru.json'): return None
        a,b=(json.loads(read(c,'locale/ru-contexts.json')) for c in (old,new))
        added={'End session':'Завершить сеанс','End this session?':'Завершить этот сеанс?','Open sessions':'Открытые сеансы','No open sessions':'Нет открытых сеансов'}
        for key,value in added.items():
            if key in a['macos-ui'] or b['macos-ui'].pop(key,None)!=value: return None
        if a!=b:return None
        a,b=(read(c,'locale/builtin.h') for c in (old,new))
        def literal(s):return '"'+''.join(chr(c) if 32<=c<127 and c not in (34,92) else f'\\{c:03o}' for c in s.encode())+'"'
        for key,value in added.items():
            row='    {'+literal(key)+', '+literal(value)+'},\n'
            if b.count(row)!=1:return None
            start=b.index('static const KrMessage macos_ui_messages[] = {')
            if not start<b.index(row)<b.index('};',start):return None
            b=b.replace(row,'',1)
        if a!=b:return None
        return dict(kind='app-session-menu-only',tested_source_commit=old,packaged_source_commit=new,changed_sources=sorted(sources),scope='Core/helper/key/network checks only; changed session-menu UI must run fresh',added_gui_strings=added)
    except (ValueError,KeyError,subprocess.CalledProcessError):return None
