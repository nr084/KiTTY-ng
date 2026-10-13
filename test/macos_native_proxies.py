"""Disposable authenticated proxies restricted to one loopback SSH endpoint."""
import base64
import select
import socket
import threading


def check_proxies(profile, ssh_port, invoke, passed):
    original=profile.read_text()
    username=b'synthetic-proxy'
    password=b'synthetic-password'
    def exact(sock,count):
        data=b''
        while len(data)<count:
            part=sock.recv(count-len(data))
            if not part:raise AssertionError('Proxy EOF')
            data+=part
        return data
    for protocol, method in (('http',3),('socks5',2)):
        errors=[]
        ready=threading.Event()
        with socket.socket() as listener:
            listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(10)
            def serve():
                try:
                    with listener.accept()[0] as client:
                        client.settimeout(10)
                        if protocol=='http':
                            header=b''
                            while not (header.endswith(b'\r\n\r\n') or header.endswith(b'\n\n')):
                                header+=exact(client,1)
                                assert len(header)<16384
                            assert header.startswith(f'CONNECT 127.0.0.1:{ssh_port} '.encode()),header
                            auth=b'proxy-authorization: basic '+base64.b64encode(username+b':'+password).lower()
                            if auth not in header.lower():
                                client.sendall(b'HTTP/1.1 407 Proxy Authentication Required\r\nProxy-Authenticate: Basic realm="synthetic"\r\nContent-Length: 0\r\n\r\n')
                                header=b''
                                while not (header.endswith(b'\r\n\r\n') or header.endswith(b'\n\n')):
                                    header+=exact(client,1)
                                    assert len(header)<16384
                            assert auth in header.lower(),header
                            client.sendall(b'HTTP/1.1 200 Connection established\r\n\r\n')
                        else:
                            assert exact(client,1)==b'\x05'
                            count=exact(client,1)[0];assert 2 in exact(client,count)
                            client.sendall(b'\x05\x02')
                            assert exact(client,1)==b'\x01'
                            assert exact(client,exact(client,1)[0])==username
                            assert exact(client,exact(client,1)[0])==password
                            client.sendall(b'\x01\x00')
                            assert exact(client,3)==b'\x05\x01\x00'
                            kind=exact(client,1)
                            host=socket.inet_ntoa(exact(client,4)) if kind==b'\x01' else exact(client,exact(client,1)[0]).decode() if kind==b'\x03' else None
                            assert host=='127.0.0.1' and int.from_bytes(exact(client,2),'big')==ssh_port
                            client.sendall(b'\x05\x00\x00\x01\x7f\x00\x00\x01'+ssh_port.to_bytes(2,'big'))
                        with socket.create_connection(('127.0.0.1',ssh_port),timeout=10) as target:
                            ready.set()
                            while True:
                                sources,_,_=select.select([client,target],[],[],10)
                                if not sources:return
                                for source in sources:
                                    data=source.recv(65536)
                                    if not data:return
                                    (target if source is client else client).sendall(data)
                except BaseException as error:errors.append(repr(error)+' '+repr(locals().get('header',b'')))
            worker=threading.Thread(target=serve,daemon=True);worker.start()
            profile.write_text(original+f'ProxyMethod\\{method}\\\nProxyHost\\127.0.0.1\\\nProxyPort\\{listener.getsockname()[1]}\\\nProxyLocalhost\\1\\\nProxyUsername\\{username.decode()}\\\nProxyPassword\\{password.decode()}\\\n')
            try:
                try:result=invoke()
                except BaseException:
                    if errors:raise AssertionError(errors)
                    raise
                assert b'NATIVE_SSH_OK' in result.stdout
                assert ready.is_set(), 'Client bypassed proxy'
                worker.join(10)
                assert not worker.is_alive() and not errors,errors
                passed(protocol+'-authenticated-loopback-proxy')
            finally:profile.write_text(original)
