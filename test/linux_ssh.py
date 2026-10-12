"""Real OpenSSH key/agent/SFTP/portable-root checks in an isolated VM account."""
import argparse,os,pwd,subprocess,tempfile,hashlib,json,time,signal,shlex
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--bin-dir',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
b=args.bin_dir.resolve();account='kittyng-test';home=Path('/opt/kitty-ng/test-home')
try:user=pwd.getpwnam(account)
except KeyError:
 subprocess.run(['useradd','-m','-d',str(home),'-s','/bin/bash',account],check=True);user=pwd.getpwnam(account)
 subprocess.run(['usermod','-p','*',account],check=True)
home.mkdir(exist_ok=True);os.chown(home,user.pw_uid,user.pw_gid)
ssh=home/'.ssh';ssh.mkdir(exist_ok=True,mode=0o700);os.chown(ssh,user.pw_uid,user.pw_gid);ssh.chmod(0o700);authorized=ssh/'authorized_keys'
def run(tool,*parameters,env=None):
 p=subprocess.run([str(b/tool),*map(str,parameters)],env=env,capture_output=True,text=True,timeout=30)
 assert p.returncode==0,(tool,p.returncode,p.stderr[-2000:]);return p.stdout
fingerprint=subprocess.check_output(['ssh-keygen','-lf','/etc/ssh/ssh_host_ed25519_key.pub'],text=True).split()[1]
report={'checks':{}}
with tempfile.TemporaryDirectory(prefix='kitty-ng-ssh-test-') as directory:
 root=Path(directory);private=root/'openssh';subprocess.run(['ssh-keygen','-q','-t','ed25519','-N','','-C','kitty-ng-integration-fixture','-f',str(private)],check=True)
 public=private.with_suffix('.pub').read_text().strip();previous=authorized.read_text() if authorized.exists() else ''
 authorized.write_text(previous+('' if not previous or previous.endswith('\n') else '\n')+public+'\n');authorized.chmod(0o600);os.chown(authorized,user.pw_uid,user.pw_gid)
 portable=root/'portable';(portable/'Sessions').mkdir(parents=True);(portable/'SSH-Keys').mkdir();ppk=portable/'SSH-Keys/fixture.ppk'
 agent=None
 try:
  run('kittygen',private,'-O','private','--new-passphrase','/dev/null','-o',ppk)
  profile={'HostName':'127.0.0.1','Protocol':'ssh','PortNumber':'22','UserName':account,'PublicKeyFile':'SSH-Keys/fixture.ppk','WinSCPProtocol':'1','UnknownLegacy':'retained'}
  (portable/'Sessions/Native').write_text(''.join(k+chr(92)+v+chr(92)+'\n' for k,v in profile.items()))
  common=['--portable-dir',str(portable),'-load','Native','-batch','-hostkey',fingerprint]
  assert run('klink',*common,'printf __KNG_SSH_OK__').strip()=='__KNG_SSH_OK__';report['checks']['real_ssh_relative_ppk']='passed'
  remote=home/'transfer-fixture';remote.mkdir(exist_ok=True);os.chown(remote,user.pw_uid,user.pw_gid)
  local=portable/'файл with spaces.txt';local.write_text('KiTTY-ng portable SFTP: кириллица\n')
  run('kscp',*common,'-sftp','--',local,account+'@127.0.0.1:'+str(remote)+'/')
  target=remote/local.name;assert target.read_bytes()==local.read_bytes();download=root/'download.txt'
  run('kscp',*common,'-sftp','--',account+'@127.0.0.1:'+str(target),download);assert download.read_bytes()==local.read_bytes();report['checks']['real_sftp_upload_download_unicode']='passed'
  moved=root/'moved portable папка';portable.rename(moved);portable=moved
  common=['--portable-dir',str(portable),'-load','Native','-batch','-hostkey',fingerprint]
  assert run('klink',*common,'printf __KNG_MOVED_OK__').strip()=='__KNG_MOVED_OK__';report['checks']['relocated_portable_key']='passed'
  socket=root/'agent.sock';agent=subprocess.Popen([str(b/'kageant'),'--foreground','--symlink',str(socket)],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True)
  deadline=time.monotonic()+10
  while time.monotonic()<deadline and not socket.exists():time.sleep(.1)
  assert socket.exists();env={**os.environ,'SSH_AUTH_SOCK':str(socket)}
  run('kageant','-a',portable/'SSH-Keys/fixture.ppk',env=env)
  assert 'kitty-ng-integration-fixture' in run('kageant','-l',env=env)
  assert run('klink','--portable-dir',portable,'-batch','-hostkey',fingerprint,'-agent',account+'@127.0.0.1','printf __KNG_AGENT_OK__',env=env).strip()=='__KNG_AGENT_OK__'
  run('kageant','-D',env=env);assert 'kitty-ng-integration-fixture' not in run('kageant','-l',env=env);report['checks']['real_agent_key_auth_add_list_clear']='passed'
  assert 'UnknownLegacy'+chr(92)+'retained'+chr(92) in (portable/'Sessions/Native').read_text();report['checks']['legacy_profile_unchanged']='passed'
 finally:
  if agent:agent.terminate();agent.wait(timeout=10)
  current=authorized.read_text();authorized.write_text(''.join(line for line in current.splitlines(keepends=True) if line.strip()!=public))
  for path in (home/'transfer-fixture').glob('*'):path.unlink()
  (home/'transfer-fixture').rmdir()
(args.output/'ssh.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',json.dumps(report['checks']))
