"""Reuse same-version helper acceptance only for byte-identical Mach-O payloads.

Ignore only the embedded 40-byte source commit, LC_UUID and native code signatures.
Version strings, executable instructions, other constants and all slice structure
remain part of the comparison. Original execution hashes/results are retained.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('verify_macos',ROOT/'tools/verify-macos.py')
validator=importlib.util.module_from_spec(spec);spec.loader.exec_module(validator)


def fingerprint(path,commit):
    assert len(commit)==40
    result={}
    for cpu,raw in validator.slices(path.read_bytes()):
        data=bytearray(raw)
        assert data[:4]==b'\xcf\xfa\xed\xfe'
        cursor=32
        for _ in range(struct.unpack_from('<I',data,16)[0]):
            command,size=struct.unpack_from('<II',data,cursor)
            assert size>=8 and cursor+size<=len(data)
            if command==0x1b:
                assert size==24;data[cursor+8:cursor+24]=bytes(16)
            elif command==0x1d:
                offset,length=struct.unpack_from('<II',data,cursor+8)
                assert offset+length<=len(data);data[offset:offset+length]=bytes(length)
            cursor+=size
        assert commit.encode() in data,'Missing embedded source commit'
        result[str(cpu)]=hashlib.sha256(bytes(data).replace(commit.encode(),b'0'*40)).hexdigest()
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode',choices=('capture','reuse'))
    parser.add_argument('--output',type=Path,default=ROOT/'dist/macos-native')
    parser.add_argument('--evidence',type=Path,required=True)
    args=parser.parse_args();dest=args.output
    build=json.loads((dest/'macos-build.json').read_text())
    assert build['status']=='success' and not build['working_tree_dirty']
    commit=build['source_commit'];helpers=dest/'KiTTY-ng.app/Contents/Helpers'
    hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in helpers.iterdir() if p.is_file()}
    payloads={name:fingerprint(helpers/name,commit) for name in hashes}
    if args.mode=='capture':
        reports={arch:json.loads((dest/f'runtime-{arch}.json').read_text()) for arch in ('arm64','x86_64')}
        for arch,report in reports.items():
            assert report['status']=='passed' and report['binaries_sha256']==hashes
        args.evidence.write_text(json.dumps(dict(build=build,payloads=payloads,execution_reports=reports),indent=2)+'\n')
        print('Saved exact same-version execution evidence:',commit)
    else:
        prior=json.loads(args.evidence.read_text());old=prior['build']
        assert old['version']==build['version']
        for key in ('compiler','sdk','deployment_target'):assert old[key]==build[key]
        assert payloads==prior['payloads'],'Helper payload changed: execute affected runtime checks'
        for arch,tested in prior['execution_reports'].items():
            report=dict(tested)
            report.update(version=build['version'],source_commit=commit,binaries_sha256=hashes,
                tested_source_commit=old['source_commit'],tested_binaries_sha256=tested['binaries_sha256'],
                execution_reused=True,reuse_proof='All helper Mach-O bytes equal after excluding only commit string, LC_UUID and code signature',
                payload_sha256=payloads)
            (dest/f'runtime-{arch}.json').write_text(json.dumps(report,indent=2)+'\n')
        print('PASS: exact helper payload equality; previous same-version SSH/SCP/SFTP/agent results reused')


if __name__=='__main__':main()
