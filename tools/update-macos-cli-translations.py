"""Generate exact Unix utility format translations; leave shell/wire syntax untouched."""
from pathlib import Path
import re,ast,json
ROOT=Path(__file__).resolve().parents[1]
phrases=json.loads((ROOT/'macos/cli-translation-phrases.json').read_text(encoding='utf8'))
pattern=r'"(?:[^"\\]|\\.)*"'
rows={}
for f in ['unix/pageant.c','unix/console.c','unix/plink.c','cmdgen.c','pscp.c','psftp.c']:
 source=(ROOT/f).read_text(encoding='utf8')
 for m in re.finditer(r'\b(?:printf|fprintf|dupprintf|cmdline_error|nonfatal)\(\s*(?:stderr,\s*|stdout,\s*)?((?:'+pattern+r'\s*)+)',source):
  en=''.join(ast.literal_eval(x) for x in re.findall(pattern,m[1]))
  if not re.search('[A-Za-z]{3}',en) or en.startswith(('SSH_AUTH_SOCK=','setenv SSH_AUTH_SOCK','scp%s')) or en=='quit\n':continue
  ru=en
  for phrase in sorted(phrases,key=len,reverse=True):ru=ru.replace(phrase,phrases[phrase])
  for old,new in [('pageant','kageant'),('Pageant','Kageant'),('puttygen','kittygen'),('pscp','kscp'),('psftp','ksftp'),('plink','klink')]:ru=ru.replace(old,new)
  if ru!=en:rows[en]=ru
out='/* Generated from reviewed phrase mappings. */\n'+''.join('{%s,%s},\n'%(json.dumps(a,ensure_ascii=False),json.dumps(b,ensure_ascii=False)) for a,b in sorted(rows.items()))
(ROOT/'macos/cli-extra-translations.h').write_text(out,encoding='utf8')
print('Registered',len(rows),'exact utility formats')
