"""Native x64 WFX download regression against generated, bounded-memory SFTP data."""
import argparse,ctypes,json,shutil,socket,stat,tempfile,threading,time,queue,struct
from pathlib import Path
import paramiko
from test_embedded import Files,Server,SFTPServer
class Generated:
    def __init__(self,size): self.size=size
    def __len__(self): return self.size
    def __getitem__(self,s):
        end=min(s.stop,self.size)
        if s.start>=end:return b''
        out=bytearray()
        for block in range(s.start//32768,(end+32767)//32768):
            data=struct.pack('<Q',block)+b'Z'*(32768-8)
            out.extend(data[max(0,s.start-block*32768):min(32768,end-block*32768)])
        return bytes(out)
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--dll',type=Path,required=True);ap.add_argument('--latency-ms',type=int,default=0);ap.add_argument('--mib',type=int,default=64);ap.add_argument('--cancel-mib',type=int,default=0);ap.add_argument('--report',type=Path,required=True);args=ap.parse_args()
    size=args.mib*1024*1024; Files.nodes['/large.bin']=[Generated(size),stat.S_IFREG|0o644,1700000000]
    class DelayedSFTP(SFTPServer):
        def __init__(self,*a,**kw):
            self.replies=queue.Queue();super().__init__(*a,**kw)
            def sender():
                while True:
                    item=self.replies.get()
                    if item is None:return
                    when,kind,data=item;time.sleep(max(0,when-time.monotonic()))
                    try:super(DelayedSFTP,self)._send_packet(kind,data)
                    except (EOFError,OSError):return
            threading.Thread(target=sender,daemon=True).start()
        def _send_packet(self,kind,data):
            if kind==103 and args.latency_ms:self.replies.put((time.monotonic()+args.latency_ms/1000,kind,data))
            else:super()._send_packet(kind,data)
        def finish_subsystem(self):
            self.replies.put(None);super().finish_subsystem()
    key=paramiko.RSAKey.generate(2048);listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(1);port=listener.getsockname()[1];clients=[]
    def serve():
        sock,_=listener.accept();sock.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
        t=paramiko.Transport(sock);clients.append(t);t.add_server_key(key);t.set_subsystem_handler('sftp',DelayedSFTP,Files);t.start_server(server=Server())
        while t.is_active():time.sleep(.01)
    threading.Thread(target=serve,daemon=True).start()
    with tempfile.TemporaryDirectory(prefix='kitty-ng-fs-large-') as tmp:
        root=Path(tmp);dll=root/args.dll.name;shutil.copy2(args.dll,dll);(root/'Sessions').mkdir();(root/'SshHostKeys').mkdir()
        values={'Protocol':'ssh','HostName':'127.0.0.1','PortNumber':str(port),'UserName':'fixture','Password':'test-only-password','Agent':'0'}
        (root/'Sessions/TestServer').write_text(''.join(f'{k}\\{v}\\\n' for k,v in values.items()),encoding='utf-8')
        (root/'kitty.ini').write_text('[KiTTY]\nsavemode=dir\ncryptsalt=2\n')
        (root/'SshHostKeys'/f'rsa2@{port}%3A127.0.0.1').write_text(f'0x{key.public_numbers.e:x},0x{key.public_numbers.n:x}')
        (root/'kittyngfs.ini').write_text('[KiTTYngFS]\nPortableDir='+str(root)+'\nRemoteRoot=/\nTimeoutSeconds=5\n',encoding='utf-16')
        api=ctypes.WinDLL(str(dll));samples=[];start=time.monotonic();logs=[];max_bytes=0
        PROGRESS=ctypes.WINFUNCTYPE(ctypes.c_int,ctypes.c_int,ctypes.c_wchar_p,ctypes.c_wchar_p,ctypes.c_int)
        LOG=ctypes.WINFUNCTYPE(None,ctypes.c_int,ctypes.c_int,ctypes.c_wchar_p)
        REQUEST=ctypes.WINFUNCTYPE(ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_wchar_p,ctypes.c_wchar_p,ctypes.c_void_p,ctypes.c_int)
        def progress(n,source,target,percent):
            nonlocal max_bytes
            parts=list(root.glob('*.part'));copied=max([f.stat().st_size for f in parts]+[0]);max_bytes=max(max_bytes,copied)
            samples.append([round(time.monotonic()-start,3),percent,copied])
            return int(time.monotonic()-start>180 or (args.cancel_mib and copied>=args.cancel_mib*1024*1024))
        cb=PROGRESS(progress);log=LOG(lambda n,k,s:logs.append([k,s]));request=REQUEST(lambda *a:0)
        api.FsInitW.argtypes=[ctypes.c_int,PROGRESS,LOG,REQUEST];api.FsInitW(0,cb,log,request)
        api.FsGetFileW.argtypes=[ctypes.c_wchar_p,ctypes.c_wchar_p,ctypes.c_int,ctypes.c_void_p]
        dest=root/'download.bin'
        if args.cancel_mib:dest.write_bytes(b'preserve-existing')
        result=api.FsGetFileW('\\TestServer\\large.bin',str(dest),1 if args.cancel_mib else 0,None);elapsed=time.monotonic()-start
        ok=(result==5 and dest.read_bytes()==b'preserve-existing') if args.cancel_mib else result==0 and dest.stat().st_size==size
        if ok and not args.cancel_mib:
            with dest.open('rb') as f:
                offset=0;generated=Generated(size)
                while data:=f.read(1024*1024):
                    if data!=generated[offset:offset+len(data)]:ok=False;break
                    offset+=len(data)
        api.FsDisconnectW.argtypes=[ctypes.c_wchar_p];api.FsDisconnectW('\\TestServer')
        ctypes.windll.kernel32.FreeLibrary.argtypes=[ctypes.c_void_p];ctypes.windll.kernel32.FreeLibrary(api._handle)
        report={'ok':ok,'result':result,'size':size,'elapsed':elapsed,'max_bytes':max_bytes,'samples':samples,'logs':logs,'leftover_parts':[f.name for f in root.glob('*.part')]}
        args.report.write_text(json.dumps(report,indent=2),encoding='utf-8')
        print(json.dumps({k:v for k,v in report.items() if k!='samples'},indent=2),flush=True)
        assert ok and not report['leftover_parts']
        assert all(a[1]<=b[1] for a,b in zip(samples,samples[1:]))
        if not args.cancel_mib:assert samples[-1][1]==100
    for t in clients:t.close()
    listener.close()
if __name__=='__main__':main()
