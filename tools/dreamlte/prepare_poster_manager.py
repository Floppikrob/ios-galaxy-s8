#!/usr/bin/env python3
"""Prepare one explicit PosterBoard registration or framework-refresh entitlement on a private copy.

No device writes. Requires a caller-supplied original SHA256, preserves every
original entitlement and all code bytes, then checks signature code/special
slots. The supported input is the pinned thin arm64 iOS PosterBoard executable.
Apple binaries and entitlement dumps must remain outside version control.
"""
from pathlib import Path
import argparse,hashlib,json,plistlib,struct,subprocess,tempfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('original',type=Path)
parser.add_argument('output',type=Path)
parser.add_argument('--ldid',required=True)
parser.add_argument('--expected-sha256',required=True)
parser.add_argument('--framework-refresh',action='store_true',help='Add the measured workspace refresh right to an already prepared PosterBoard')
args=parser.parse_args()
assert args.original.resolve()!=args.output.resolve()
old=args.original.read_bytes()
assert hashlib.sha256(old).hexdigest()==args.expected_sha256
oldent=plistlib.loads(subprocess.check_output([args.ldid,'-e',str(args.original)]))
key='com.apple.lsapplicationworkspace.rebuildappdatabases' if args.framework_refresh else 'com.apple.private.pluginkit.manager'
if args.framework_refresh:assert oldent.get('com.apple.private.pluginkit.manager') is True
assert key not in oldent
newent=dict(oldent);newent[key]=True
with tempfile.TemporaryDirectory(prefix='.poster-manager-',dir=args.output.parent) as tmp:
    tmp=Path(tmp);ent=tmp/'entitlements.plist';ent.write_bytes(plistlib.dumps(newent))
    binary=tmp/'PosterBoard';binary.write_bytes(old)
    subprocess.run([args.ldid,'-Cadhoc','-Hsha256','-Icom.apple.PosterBoard','-S'+str(ent),str(binary)],check=True)
    fresh=binary.read_bytes()
    assert len(fresh)<=len(old)
    got=plistlib.loads(subprocess.check_output([args.ldid,'-e',str(binary)]))
assert got==newent and {k:v for k,v in got.items() if k!=key}==oldent

def commands(d):
 assert struct.unpack_from('<I',d)[0]==0xfeedfacf
 pos=32;result=[]
 for i in range(struct.unpack_from('<I',d,16)[0]):
  c,n=struct.unpack_from('<II',d,pos);assert n>=8;result.append((pos,c,d[pos:pos+n]));pos+=n
 return result
a=commands(old);b=commands(fresh);assert len(a)==len(b)
old_sig=new_sig=None;allowed=set()
for (pa,ca,da),(pb,cb,db) in zip(a,b):
 assert pa==pb and ca==cb and len(da)==len(db)
 if ca==0x1d:
  old_sig=struct.unpack_from('<II',da,8);new_sig=struct.unpack_from('<II',db,8);allowed.update(range(pa+8,pa+16))
 elif ca==0x19 and da[8:24].split(b'\0')[0]==b'__LINKEDIT':
  # The signer may shorten only LINKEDIT vmsize/filesize to the new signature.
  for off in (32,48):allowed.update(range(pa+off,pa+off+8))
  assert da[:32]==db[:32] and da[40:48]==db[40:48] and da[56:]==db[56:]
 else:assert da==db,(hex(ca),pa)
assert old_sig and new_sig and old_sig[0]==new_sig[0]
old_ids=[]
sigoff=old_sig[0]
assert struct.unpack_from('>I',old,sigoff)[0]==0xfade0cc0
for i in range(struct.unpack_from('>I',old,sigoff+8)[0]):
    kind,delta=struct.unpack_from('>II',old,sigoff+12+8*i)
    if kind not in (0,0x1000):continue
    at=sigoff+delta
    ident=struct.unpack_from('>I',old,at+20)[0]
    old_ids.append(old[at+ident:].split(b'\0')[0])
assert old_ids and set(old_ids)=={b'com.apple.PosterBoard'}
limit=new_sig[0]
diff=[i for i in range(limit) if old[i]!=fresh[i]];assert diff and set(diff)<=allowed
# All executable/constant/resource bytes outside those load-command fields
# remain byte-identical. CodeDirectory identifier and all code/special hashes
# are independently checked, including the entitlement blob's special slot.
off,n=new_sig;magic,length,count=struct.unpack_from('>III',fresh,off);assert magic==0xfade0cc0 and length<=n
blobs={}
for i in range(count):
 kind,delta=struct.unpack_from('>II',fresh,off+12+8*i);ln=struct.unpack_from('>I',fresh,off+delta+4)[0];blobs[kind]=fresh[off+delta:off+delta+ln]
cd=blobs[0];vals=struct.unpack_from('>9I4B',cd);_,cdlen,version,flags,hashoff,identoff,special,slots,limit,hashsize,hashtype,platform,pageshift=vals
assert hashtype==2 and hashsize==32 and pageshift==12 and limit==off and flags&2
assert cd[identoff:].split(b'\0')[0]==b'com.apple.PosterBoard'
assert slots==(limit+4095)//4096
for i in range(slots):assert hashlib.sha256(fresh[i*4096:min((i+1)*4096,limit)]).digest()==cd[hashoff+i*32:hashoff+(i+1)*32]
assert 5 in blobs and special>=5
for kind,blob in blobs.items():
 if 0<kind<=special:assert hashlib.sha256(blob).digest()==cd[hashoff-kind*32:hashoff-(kind-1)*32]
new=fresh+bytes(len(old)-len(fresh))
report=dict(old_sha256=hashlib.sha256(old).hexdigest(),new_sha256=hashlib.sha256(new).hexdigest(),logical_size=len(old),new_macho_size=len(fresh),padding=len(new)-len(fresh),added_entitlement=key,original_entitlement_count=len(oldent),all_original_entitlements_preserved=True,only_load_command_signature_lengths_changed=diff,code_limit=limit,code_slots=slots,all_page_and_special_hashes_valid=True,identifier='com.apple.PosterBoard',signature_flags=flags,device_written=False)
args.output.write_bytes(new)
args.output.with_suffix('.validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
