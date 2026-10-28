"""Native CLI authentication/localization against a synthetic SSH server."""
import base64,hashlib,logging,socket,subprocess,sys,tempfile,threading
from pathlib import Path
import paramiko
logging.getLogger("paramiko").setLevel(logging.CRITICAL)
binary=Path(sys.argv[1]).resolve()
key=paramiko.RSAKey.generate(2048)
fingerprint="SHA256:"+base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")
class Server(paramiko.ServerInterface):
    def check_auth_password(self,user,password):
        return paramiko.AUTH_SUCCESSFUL if (user,password)==("synthetic","test-only") else paramiko.AUTH_FAILED
    def get_allowed_auths(self,user): return "password"
    def check_channel_request(self,kind,channel): return paramiko.OPEN_SUCCEEDED
    def check_channel_exec_request(self,channel,command):
        self.ready.set(); return True
for language in ("ru","en"):
    with tempfile.TemporaryDirectory(prefix="kitty-ng-language-") as folder:
        listener=socket.socket();listener.bind(("127.0.0.1",0));listener.listen(1);listener.settimeout(15)
        failures=[]
        def serve():
            transport=None
            try:
                peer,_=listener.accept();transport=paramiko.Transport(peer);transport.add_server_key(key)
                server=Server();server.ready=threading.Event();transport.start_server(server=server)
                channel=transport.accept(15);assert channel and server.ready.wait(15)
                channel.sendall(b"SERVER RAW: Access denied\n");channel.send_exit_status(0);channel.close()
            except Exception as error:failures.append(repr(error))
            finally:
                if transport:transport.close()
        thread=threading.Thread(target=serve);thread.start()
        path=Path(folder)/"Ключи с пробелами"/"проверка.ppk"
        result=subprocess.run([str(binary),"--portable-dir",folder,"--language",language,"-batch","-v","-ssh","127.0.0.1","-P",str(listener.getsockname()[1]),"-l","synthetic","-pw","test-only","-hostkey",fingerprint,"-i",str(path),"synthetic-command"],capture_output=True,timeout=25)
        thread.join(16);listener.close();assert not thread.is_alive() and not failures,failures
        text=result.stderr.decode("utf8");assert result.returncode==0,text
        assert result.stdout==b"SERVER RAW: Access denied\n",result.stdout
        assert str(path) in text and "\ufffd" not in text,text
        expected=('Имя пользователя: «synthetic».','Используется протокол SSH','Доступ разрешён') if language=='ru' else ('Using username "synthetic".','Using SSH protocol version','Access granted')
        for phrase in expected:assert phrase in text,text
        if language=='ru':
            for phrase in ('Using username','Reading key file','Access granted','Using SSH protocol version'):assert phrase not in text,text
        print('PASS: '+language+' CLI login, Unicode key path, translated diagnostics, unchanged server payload',flush=True)
