"""Validate Universal 2 Mach-O code hashes and ad-hoc bundle seals without macOS.

This verifies structural/hash integrity, not Gatekeeper policy or native execution.
CodeDirectory layout: apple-oss-distributions/xnu/osfmk/kern/cs_blobs.h.
"""
import hashlib,json,plistlib,struct,sys
from pathlib import Path

def slices(data):
    magic,count=struct.unpack_from('>II',data)
    if magic==0xcafebabe:
        for i in range(count):
            cpu,sub,offset,size,align=struct.unpack_from('>5I',data,8+20*i)
            assert offset+size<=len(data),'Invalid fat slice range'
            yield cpu,data[offset:offset+size]
    else:raise ValueError('Expected Universal 2 fat Mach-O')
def verify_binary(path,bundle=None):
    data=Path(path).read_bytes();architectures=[];cdhashes=set()
    for cpu,thin in slices(data):
        architectures.append(cpu);magic,actual_cpu,sub,filetype,commands,total,flags,reserved=struct.unpack_from('<8I',thin)
        assert magic==0xfeedfacf and actual_cpu==cpu,'Invalid Mach-O header'
        pos=32;signature=None
        for _ in range(commands):
            command,size=struct.unpack_from('<II',thin,pos)
            assert size>=8 and pos+size<=32+total,'Invalid load command'
            if command==0x1d:
                offset,length=struct.unpack_from('<II',thin,pos+8);assert offset+length<=len(thin)
                signature=thin[offset:offset+length]
            pos+=size
        assert signature,'Missing code signature'
        magic,length,count=struct.unpack_from('>3I',signature);assert magic==0xfade0cc0 and length<=len(signature)
        blobs={}
        for i in range(count):
            kind,offset=struct.unpack_from('>II',signature,12+8*i);blob_magic,blob_length=struct.unpack_from('>II',signature,offset)
            assert offset+blob_length<=length;blobs[kind]=signature[offset:offset+blob_length]
        directories=[blob for kind,blob in blobs.items() if kind==0 or 0x1000<=kind<=0x1005];assert directories
        for cd in directories:
            magic,length,version,flags,hash_offset,ident,special,slots,limit,hash_size,hash_type,platform,page_bits,spare=struct.unpack_from('>9I4BI',cd)
            assert magic==0xfade0c02 and flags&2,'Expected ad-hoc CodeDirectory'
            if version>=0x20300 and not limit:limit=struct.unpack_from('>Q',cd,60)[0]
            hash_fn={1:hashlib.sha1,2:hashlib.sha256,3:hashlib.sha256,4:hashlib.sha384}.get(hash_type);assert hash_fn,'Unknown hash type'
            assert page_bits<=20 and hash_offset+slots*hash_size<=len(cd) and limit<=len(thin)
            assert hash_size=={1:20,2:32,3:20,4:48}[hash_type] and hash_offset>=special*hash_size and limit>0
            cdhashes.add(hash_fn(cd).digest()[:20])
            page=1<<page_bits if page_bits else limit
            assert slots==(limit+page-1)//page
            for i in range(slots):
                expected=cd[hash_offset+i*hash_size:hash_offset+(i+1)*hash_size]
                assert hash_fn(thin[i*page:min((i+1)*page,limit)]).digest()[:hash_size]==expected,'Changed signed code page'
            for kind in range(1,special+1):
                expected=cd[hash_offset-kind*hash_size:hash_offset-(kind-1)*hash_size]
                if not any(expected):continue
                value=blobs.get(kind)
                if bundle and kind==1:value=(Path(bundle)/'Contents/Info.plist').read_bytes()
                if bundle and kind==3:value=(Path(bundle)/'Contents/_CodeSignature/CodeResources').read_bytes()
                if value is not None:assert hash_fn(value).digest()[:hash_size]==expected,'Changed signature special slot'
        assert 0 in blobs
    assert set(architectures)=={0x1000007,0x100000c} and len(architectures)==2,'Expected exactly x86_64 and arm64'
    return cdhashes

def verify_bundle(path):
    app=Path(path);info=plistlib.loads((app/'Contents/Info.plist').read_bytes());exe=app/'Contents/MacOS'/info['CFBundleExecutable'];verify_binary(exe,app)
    resources=plistlib.loads((app/'Contents/_CodeSignature/CodeResources').read_bytes())
    for name,entry in resources.get('files2',{}).items():
        p=(app/'Contents'/name).resolve();assert p.is_relative_to((app/'Contents').resolve()),'Escaping resource path'
        if isinstance(entry,dict) and 'hash2' in entry:assert hashlib.sha256(p.read_bytes()).digest()==entry['hash2'],'Changed sealed resource: '+name
        elif isinstance(entry,dict) and 'hash' in entry:assert hashlib.sha1(p.read_bytes()).digest()==entry['hash'],'Changed sealed resource: '+name
        elif isinstance(entry,dict) and 'cdhash' in entry:assert entry['cdhash'] in verify_binary(p),'Changed nested code seal: '+name
    for helper in (app/'Contents/Helpers').glob('*'):
        if helper.is_file():verify_binary(helper)
    print('PASS: Universal 2 ad-hoc code page hashes, Info.plist and sealed bundle resources')
if __name__=='__main__':verify_bundle(sys.argv[1])
