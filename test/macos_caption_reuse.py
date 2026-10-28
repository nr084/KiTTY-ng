"""Caption reuse must reject code, translation, test and version changes."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('caption', root/'tools/macos-caption-reuse.py')
api = importlib.util.module_from_spec(spec)
spec.loader.exec_module(api)

with tempfile.TemporaryDirectory(prefix='kitty-ng-caption-proof-') as tmp:
    repo = Path(tmp)
    def run(*args):
        return subprocess.check_output(['git', *args], cwd=repo, stderr=subprocess.DEVNULL).decode().strip()
    run('init', '-b', 'codex/caption-proof')
    def write(path, text):
        p=repo/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text)
    def commit():
        run('add', '.');run('-c','user.name=Test','-c','user.email=test@example.invalid','commit','-m','fixture')
        return run('rev-parse','HEAD')
    write('VERSION','0.85-10\n')
    write('macos/bootstrap.m','before\n'+api.OLD_LINE+'\nafter\n')
    write('macos/client.m','unchanged code\n')
    write('locale/ru.json','{}\n')
    write('locale/ru-contexts.json',json.dumps({'macos-ui':{},'macos-cli':{}}))
    header='static const KrMessage macos_ui_messages[] = {\n};\n'
    write('locale/builtin.h',header)
    old=commit()
    write('macos/bootstrap.m','before\n'+api.NEW_LINE+'\nafter\n')
    write('locale/ru-contexts.json',json.dumps({'macos-ui':{api.KEY:api.VALUE},'macos-cli':{}}))
    write('locale/builtin.h',header.replace('};','    {'+api.literal(api.KEY)+', '+api.literal(api.VALUE)+'},\n};'))
    safe=commit();assert api.caption_proof(repo,old,safe)
    write('macos/client.m','changed routing\n');bad=commit();assert api.caption_proof(repo,old,bad) is None
    write('macos/client.m','unchanged code\n');write('locale/ru.json',json.dumps({'Other':'Изменено'}));bad=commit();assert api.caption_proof(repo,old,bad) is None
    write('locale/ru.json','{}\n');write('test/macos_native_runtime.py','changed checks\n');bad=commit();assert api.caption_proof(repo,old,bad) is None
    (repo/'test/macos_native_runtime.py').unlink();write('VERSION','0.85-11\n');bad=commit();assert api.caption_proof(repo,old,bad) is None
    write('VERSION','0.85-10\n');write('macos/bootstrap.m','changed behavior\n'+api.NEW_LINE+'\nafter\n');bad=commit();assert api.caption_proof(repo,old,bad) is None
print('PASS: caption-only proof accepts the narrow UI change and rejects unrelated behavior, catalog, test, version and bootstrap changes')
