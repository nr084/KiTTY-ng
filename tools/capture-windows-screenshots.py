"""Capture native owned Windows demo dialogs with synthetic data only."""
import argparse,subprocess,tempfile,os
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);args=p.parse_args()
out=ROOT/'docs/screenshots';exe=args.build.resolve()/'kitty-ng-x64.exe';keygen=args.build.resolve()/'kittygen-x64.exe'
with tempfile.TemporaryDirectory(prefix='release-screenshots-',dir=ROOT/'build') as temp:
 env=dict(os.environ,KITTY_NG_PORTABLE_DIR=temp)
 for lang in ('ru','en'):
  for name,panel in [('session','Session'),('language','Window/Translation'),('transfer','Connection/File transfer'),('automation','Connection/Automation'),('extensions','Window/Extensions'),('proxies','Connection/Proxy profiles')]:
   subprocess.run([str(exe),'--portable-dir',temp,'--language',lang,'-demo-config-box',str(out/f'{name}-{lang}.png'),'-demo-panel',panel],env=env,check=True,timeout=30)
  subprocess.run([str(exe),'--portable-dir',temp,'--language',lang,'-demo-about',str(out/f'about-{lang}.png')],env=env,check=True,timeout=30)
  subprocess.run([str(keygen),'--language',lang,'-demo-screenshot',str(out/f'kittygen-{lang}.png')],env=env,check=True,timeout=30)
  for image in out.glob('*-'+lang+'.png'):
   if image.name.startswith(('macos-','linux-')):continue
   with Image.open(image) as source:pixels=source.convert('RGB')
   pixels.save(image,format='PNG',optimize=True)
print('Captured Windows dialogs; lossless PNG encoding, original pixels preserved.')
