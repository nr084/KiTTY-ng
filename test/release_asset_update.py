"""Offline safety regression for updating published assets in place."""
import ast,hashlib,tempfile,types,urllib.parse
from pathlib import Path
source=Path(__file__).resolve().parents[1]/"tools/publish-release.py"
node=next(n for n in ast.parse(source.read_text()).body if isinstance(n,ast.If) and ast.unparse(n.test)=="args.update_current" and any(isinstance(x,ast.For) for x in n.body))
code=compile(ast.Module(body=node.body,type_ignores=[]),str(source),"exec")
for corrupt in (False,True):
 with tempfile.TemporaryDirectory() as folder:
  root=Path(folder);(root/'dist').mkdir();data=b'new verified package';name='client.zip';(root/'dist'/name).write_bytes(data)
  digest=hashlib.sha256(data).hexdigest();events=[]
  release={'id':1,'upload_url':'https://upload.invalid/assets{?name}', 'assets':[{'id':2,'name':name,'digest':'sha256:old'}]}
  def request(url,method='GET',data=None,content_type=None):
   events.append((method,url))
   if method=='POST':return {'id':3,'size':len(data),'digest':'sha256:'+('bad' if corrupt else digest)}
   if method=='DELETE':assert events[0][0]=='POST';return None
   if method=='PATCH':return release
   raise AssertionError((method,url))
  scope=dict(ROOT=root,report={'archives':[{'file':name,'sha256':digest}]},release=release,API='https://api.invalid',body='notes',request=request,urllib=urllib)
  try:exec(code,scope)
  except AssertionError:
   assert corrupt and not any(method=='DELETE' for method,url in events)
  else:
   assert not corrupt
   assert [method for method,url in events]==['POST','DELETE','PATCH','PATCH']
   assert not any('/git/' in url for method,url in events)
print('PASS: verify upload before deletion; failed checksum preserves old asset; no tag mutation')
