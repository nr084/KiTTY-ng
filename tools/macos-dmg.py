# Create an HFS+ Universal DMG on Windows with Darwin modes and intact seals.
import argparse,hashlib,importlib.util,json,plistlib,shutil,subprocess,sys,tempfile,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(data):return hashlib.sha256(data).hexdigest()
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,default=ROOT/'dist');args=parser.parse_args()
 out=args.output.resolve();version=(ROOT/'VERSION').read_text().strip();report=json.loads((out/'macos-build.json').read_text());source=out/report['archive'];assert report['version']==version and report['status']=='success'
 if source.exists():assert sha(source.read_bytes())==report['sha256']
 candidates=sorted((ROOT/'_tools/macos/dmg').glob('v*/toolchain-info.json'),key=lambda p:tuple(map(int,p.parent.name[1:].split('.'))));metadata=json.loads(candidates[-1].read_text());tool=candidates[-1].parent/'apfs.exe'
 sys.path.insert(0,str(ROOT/'_tools/macos/dmg/python'));from ds_store import DSStore
 destination=out/f'kitty-ng-{version}-macos-universal.dmg'
 with tempfile.TemporaryDirectory(prefix='macos-dmg-',dir=ROOT/'build') as temporary:
  work=Path(temporary);payload=work/'payload';carrier=work/'metadata';payload.mkdir();carrier.mkdir();modes={}
  if source.exists():
   with zipfile.ZipFile(source) as z:
    for entry in z.infolist():
     path=payload/entry.filename;assert path.resolve().is_relative_to(payload.resolve())
     if entry.is_dir():continue
     path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(z.read(entry));modes[entry.filename]=entry.external_attr>>16
  else:
   prior=json.loads((out/'macos-dmg.json').read_text());assert prior['version']==version and sha(destination.read_bytes())==prior['sha256']
   prior_metadata=work/'prior-metadata'
   subprocess.run([str(tool),'extract',str(destination),'-C',str(payload),'--preserve-meta','--metadata-root',str(prior_metadata),'--verify'],check=True,capture_output=True)
   manifest=json.loads((prior_metadata/'manifest.json').read_text());modes={record['original']:record['darwin']['mode'] for record in manifest['records'] if record['kind']=='file'}
  instructions=payload/'Instructions';instructions.mkdir(exist_ok=True)
  for name in ('README-macos.txt','Diagnose KiTTY-ng.command','Smoke-test KiTTY-ng.command'):
   path=payload/name
   if path.exists():shutil.move(path,instructions/name);modes['Instructions/'+name]=modes.pop(name)
  # Mirror the documented directory layout so offline links resolve.
  for name in ('macos.md','macos.en.md'):(instructions/name).unlink(missing_ok=True)
  shutil.copytree(ROOT/'docs',instructions/'docs',dirs_exist_ok=True)
  for name in ('README.md','README.en.md','LICENCE','LICENCE-KiTTY.txt'):shutil.copy2(ROOT/name,instructions/name)
  icons=instructions/'icons';icons.mkdir(exist_ok=True)
  for name in ('kitty-ng.png','kitty-ng-forum.png','kitty-ng-prompt.txt'):shutil.copy2(ROOT/'icons'/name,icons/name)
  shutil.copy2(payload/'KiTTY-ng.app/Contents/Resources/kitty-ng.icns',payload/'.VolumeIcon.icns')
  (instructions/'Start here.html').write_text(f'''<!doctype html><html lang="ru"><meta charset="utf-8"><title>KiTTY-ng {version} — Universal</title><style>body{{font:17px -apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;color:#18253d;background:#f2f6fb;margin:0;padding:48px}}main{{max-width:820px;margin:auto;background:white;padding:36px;border-radius:22px;box-shadow:0 12px 44px #10244d15}}h1{{margin:0;color:#175cc5;font-size:38px}}p,li{{line-height:1.6}}.chip{{display:inline-block;background:#e7efff;padding:6px 14px;border-radius:20px;color:#175cc5}}hr{{border:0;border-top:1px solid #dde5ef;margin:28px 0}}</style><main><h1>KiTTY-ng {version}</h1><p class="chip">macOS Universal · Apple Silicon + Intel</p><h2>Портативный запуск</h2><ol><li>Перенесите <b>KiTTY-ng.app</b> из образа в свою папку. Для общих профилей — рядом с Windows EXE и Sessions.</li><li>Извлеките DMG, затем откройте приложение.</li><li>Если macOS блокирует запуск: Настройки системы → Конфиденциальность и безопасность → Всё равно открыть.</li></ol><p>Данные хранятся снаружи .app. Диагностические command-файлы перенесите рядом с приложением перед запуском.</p><hr><h2>Portable setup</h2><ol><li>Copy <b>KiTTY-ng.app</b> into your folder, beside Windows EXEs and Sessions for shared profiles.</li><li>Eject the DMG, then launch the app.</li><li>If blocked, use System Settings → Privacy &amp; Security → Open Anyway.</li></ol><p>Settings stay outside the app bundle. Copy diagnostic command files beside the app before running.</p><p>Based on PuTTY by Simon Tatham and contributors, and KiTTY by Cyril Dupont. <a href="https://github.com/nr084/kitty-ng">GitHub</a></p></main></html>''',encoding='utf-8')
  with DSStore.open(str(payload/'.DS_Store'),'w+') as ds:
   ds['.']['bwsp']={'WindowBounds':'{{220, 160}, {680, 420}}','ShowToolbar':False,'ShowSidebar':False,'ShowStatusBar':False,'ShowPathbar':False}
   ds['.']['icvp']={'viewOptionsVersion':1,'iconSize':100.0,'textSize':15.0,'labelOnBottom':True,'showIconPreview':True,'arrangeBy':'none','backgroundType':1,'backgroundColorRed':.949,'backgroundColorGreen':.965,'backgroundColorBlue':.984,'gridSpacing':130.0}
   ds['.']['vstl']=('type',b'icnv');ds['KiTTY-ng.app']['Iloc']=(220,170);ds['Instructions']['Iloc']=(460,170)
  records=[]
  for path in [payload]+sorted(payload.rglob('*')):
   relative='.' if path==payload else path.relative_to(payload).as_posix();kind='directory' if path.is_dir() else 'file';mode=0o40755 if path.is_dir() else modes.get(relative,0o100644)
   record={'original':relative,'materialized':relative,'kind':kind,'materializedKind':kind,'darwin':{'mode':mode,'uid':0,'gid':0}}
   if path.is_file():data=path.read_bytes();record['payload']={'sha256':sha(data),'size':len(data)}
   records.append(record)
  (carrier/'manifest.json').write_text(json.dumps({'version':1,'generation':1,'records':records})+'\n')
  packed=subprocess.run([str(tool),'pack',str(payload),str(destination),'--fs','hfs+','--compression','zlib','--volname','KiTTY-ng '+version+' Universal','--metadata-root',str(carrier),'--strict','--output','json'],capture_output=True,text=True,encoding='utf-8');print(packed.stdout[-2000:]);print(packed.stderr[-1000:]);assert packed.returncode==0,'DMG packing failed'
  extracted=work/'extracted';verified=work/'verified-metadata'
  run=subprocess.run([str(tool),'extract',str(destination),'-C',str(extracted),'--preserve-meta','--xattrs','--metadata-root',str(verified),'--verify','--output','json'],capture_output=True,text=True,encoding='utf-8');assert run.returncode==0,run.stderr
  actual=json.loads((verified/'manifest.json').read_text());lookup={x['original']:x for x in actual['records']}
  for record in records:
   if record['kind']=='file':assert sha((extracted/record['original']).read_bytes())==record['payload']['sha256']
   if record['original'] in modes and modes[record['original']]&0o111:assert lookup[record['original']]['darwin']['mode']&0o111
  spec=importlib.util.spec_from_file_location('verify_macos',ROOT/'tools/verify-macos.py');validator=importlib.util.module_from_spec(spec);spec.loader.exec_module(validator);validator.verify_bundle(extracted/'KiTTY-ng.app')
  info=plistlib.loads((extracted/'KiTTY-ng.app/Contents/Info.plist').read_bytes());assert info['CFBundleVersion']==version.replace('-','.')
  assert destination.read_bytes()[-512:-508]==b'koly'
  result={'version':version,'status':'success','file':destination.name,'sha256':sha(destination.read_bytes()),'size':destination.stat().st_size,'architectures':['arm64','x86_64'],'filesystem':'HFS+','compression':'zlib','tool':metadata,'input_archive_sha256':report['sha256'],'bundle_hashes':'verified after extraction','darwin_execute_modes':'verified','native_mount_verified':False}
  (out/'macos-dmg.json').write_text(json.dumps(result,indent=2)+'\n')
 print('PASS: HFS+ Universal DMG, complete round-trip, executable modes and bundle seals:',destination)
if __name__=='__main__':main()
