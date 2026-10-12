"""Key-authenticated Linux build/test host access. Host credentials stay outside Git."""
import argparse,json,sys,shlex,tarfile,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'_tools/linux/python'))
import paramiko
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host',required=True);parser.add_argument('--user',default='root');parser.add_argument('--sync',action='store_true');parser.add_argument('--command');parser.add_argument('--download',nargs=2);parser.add_argument('--upload',nargs=2)
args=parser.parse_args()
client=paramiko.SSHClient();client.load_host_keys(str(ROOT/'_tools/linux/ssh/known_hosts'));client.connect(args.host,username=args.user,key_filename=str(ROOT/'_tools/linux/ssh/id_ed25519'),timeout=15,allow_agent=False,look_for_keys=False)
try:
 if args.sync:
  with tempfile.TemporaryDirectory(prefix='linux-sync-',dir=ROOT/'build') as directory:
   archive=Path(directory)/'source.tar.gz'
   with tarfile.open(archive,'w:gz') as bundle:
    for p in ROOT.rglob('*'):
     rel=p.relative_to(ROOT)
     if p.is_file() and rel.parts[0] not in ('_tools','build','dist','.git','.codex-remote-attachments') and '__pycache__' not in rel.parts:
      bundle.add(p,arcname=rel.as_posix(),recursive=False)
   with client.open_sftp() as sftp:sftp.put(str(archive),'/opt/kitty-ng/source.tar.gz')
   stdin,out,err=client.exec_command('tar -xzf /opt/kitty-ng/source.tar.gz -C /opt/kitty-ng/src && rm /opt/kitty-ng/source.tar.gz')
   print(out.read().decode(),end='');errors=err.read().decode();assert out.channel.recv_exit_status()==0,errors
   print('Synced source tree without local tools, Git, builds or credentials')
 if args.download:
  remote,local=args.download;path=Path(local);path.parent.mkdir(parents=True,exist_ok=True)
  with client.open_sftp() as sftp:sftp.get(remote,str(path))
 if args.upload:
  local,remote=args.upload
  with client.open_sftp() as sftp:sftp.put(local,remote)
 if args.command:
  stdin,out,err=client.exec_command(args.command)
  for line in iter(out.readline,''):print(line,end='',flush=True)
  errors=err.read().decode();print(errors,end='',file=sys.stderr)
  raise SystemExit(out.channel.recv_exit_status())
finally:client.close()
