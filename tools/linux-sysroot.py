"""Import Linux target headers/libraries for a local Windows build over pinned SSH.

This tool does not compile remotely or change the target operating system.
The target dependency root must already contain GTK 3 development packages.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shlex
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / '_tools/linux/python'))
import paramiko


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--user', default='root')
    parser.add_argument('--remote-root', required=True)
    parser.add_argument('--output', type=Path, default=ROOT / '_tools/linux')
    args = parser.parse_args()
    if not args.remote_root.startswith('/'):
        parser.error('--remote-root must be absolute')
    args.output.mkdir(parents=True, exist_ok=True)
    client = paramiko.SSHClient()
    client.load_host_keys(str(ROOT / '_tools/linux/ssh/known_hosts'))
    client.connect(args.host, username=args.user,
                   key_filename=str(ROOT / '_tools/linux/ssh/id_ed25519'),
                   timeout=15, look_for_keys=False, allow_agent=False)
    temporary = None
    def execute(command):
        _, output, errors = client.exec_command(command)
        result = output.read().decode()
        if output.channel.recv_exit_status():
            raise RuntimeError(errors.read().decode())
        return result.strip()
    def target(command):
        return execute('chroot ' + shlex.quote(args.remote_root) + ' sh -c ' + shlex.quote(command))
    try:
        metadata = {'architecture': 'x86_64', 'os_release': target('cat /etc/os-release'),
                    'gtk_cflags': target('pkg-config --cflags gtk+-3.0'),
                    'gtk_libs': target('pkg-config --libs gtk+-3.0'),
                    'packages': target("dpkg-query -W -f='${Package} ${Version}\\n'")}
        if target('dpkg --print-architecture') != 'amd64':
            raise ValueError('This build pipeline currently targets x86_64 only')
        temporary = execute('mktemp -d /tmp/kitty-ng-sysroot.XXXXXX')
        if not re.fullmatch(r'/tmp/kitty-ng-sysroot\.[A-Za-z0-9]+', temporary):
            raise ValueError('Unexpected remote temporary path')
        remote = temporary + '/sysroot.tar.gz'
        paths = ['usr/include', 'usr/lib', 'usr/lib64', 'lib', 'lib64',
                 'usr/share/icons/Adwaita', 'usr/share/icons/hicolor',
                 'usr/share/glib-2.0/schemas', 'usr/share/doc']
        execute('tar -czf ' + shlex.quote(remote) + ' -C ' + shlex.quote(args.remote_root) + ' ' + ' '.join(paths))
        digest = execute('sha256sum ' + shlex.quote(remote)).split()[0]
        archive = args.output / 'sysroot.tar.gz'
        with client.open_sftp() as sftp:
            sftp.get(remote, str(archive))
        actual = hashlib.sha256(archive.read_bytes()).hexdigest()
        if actual != digest:
            raise ValueError('Sysroot transfer checksum mismatch')
        metadata['archive_sha256'] = digest
        (args.output / 'target.json').write_text(json.dumps(metadata, indent=2) + '\n')
        spec = importlib.util.spec_from_file_location('linux_cross_build', ROOT / 'tools/linux-cross-build.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.unpack_sysroot(archive, args.output / 'sysroot')
        print('Target sysroot imported for local Windows compilation')
    finally:
        if temporary and re.fullmatch(r'/tmp/kitty-ng-sysroot\.[A-Za-z0-9]+', temporary):
            execute('rm -f ' + shlex.quote(temporary + '/sysroot.tar.gz') + ' && rmdir ' + shlex.quote(temporary))
        client.close()


if __name__ == '__main__':
    main()
