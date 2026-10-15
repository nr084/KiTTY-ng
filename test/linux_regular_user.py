"""Run the packaged GUI acceptance test as the isolated, non-sudo test account."""
import argparse,os,pwd,secrets,subprocess,shlex
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--binary',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--quick-only',action='store_true');args=parser.parse_args()
account='kittyng-test';user=pwd.getpwnam(account);args.output.mkdir(parents=True,exist_ok=True);os.chown(args.output,user.pw_uid,user.pw_gid)
runtime=Path('/run/user')/str(user.pw_uid);runtime.mkdir(exist_ok=True);os.chown(runtime,user.pw_uid,user.pw_gid);runtime.chmod(0o700)
password=secrets.token_urlsafe(24);subprocess.run(['chpasswd'],input=account+':'+password+'\n',text=True,check=True)
test=Path(__file__).with_name('linux_ssh_gui.py')
command='xfwm4 >/tmp/kitty-ng-regular-test-wm.log 2>&1 & exec python3 '+shlex.quote(str(test))+' --binary '+shlex.quote(str(args.binary))+' --output '+shlex.quote(str(args.output))+' --existing-account'+(' --quick-only' if args.quick_only else '')
try:
 subprocess.run(['runuser','-u',account,'--','env','XDG_RUNTIME_DIR='+str(runtime),'xvfb-run','-a','dbus-run-session','--','sh','-c',command],input=password+'\n',text=True,check=True)
 print('PASS: full packaged GUI acceptance as a regular user',flush=True)
finally:subprocess.run(['usermod','-p','*',account],check=True)
