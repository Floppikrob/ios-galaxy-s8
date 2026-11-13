#!/usr/bin/env python3
"""Prepare same-length launchd cache tuning; never writes phone partitions.
Keep the original file and verify its extents/hash before any offline installation.
"""
import argparse,copy,hashlib,json,plistlib,struct
from pathlib import Path
LABELS={'com.apple.locationd','com.apple.WirelessRadioManager'}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('output',type=Path);p.add_argument('--seconds',type=int,choices=[30,60,120,300],default=60);a=p.parse_args()
 if a.source.resolve()==a.output.resolve():p.error('preserve the original: output must differ from source')
 raw=a.source.read_bytes()
 if not raw.startswith(b'bplist00'):p.error('expected binary launchd cache')
 original=plistlib.loads(raw);new=copy.deepcopy(original);changes=[]
 for job in new['LaunchDaemons'].values():
  if job.get('Label') in LABELS:
   if 'ThrottleInterval' not in job:p.error('target has no existing ThrottleInterval')
   changes.append({'label':job['Label'],'old':job['ThrottleInterval'],'new':a.seconds});job['ThrottleInterval']=a.seconds
 if {j['label'] for j in changes}!=LABELS:p.error('expected both target labels')
 encoded=plistlib.dumps(new,fmt=plistlib.FMT_BINARY,sort_keys=True)
 if len(encoded)>len(raw):p.error('candidate grows file: filesystem-aware installation required')
 table=struct.unpack_from('>Q',encoded,len(encoded)-8)[0];pad=len(raw)-len(encoded)
 encoded=encoded[:table]+bytes(pad)+encoded[table:];encoded=encoded[:-8]+struct.pack('>Q',table+pad)
 assert len(encoded)==len(raw) and plistlib.loads(encoded)==new
 a.output.write_bytes(encoded)
 print(json.dumps({'changes':changes,'bytes':len(encoded),'old_sha256':hashlib.sha256(raw).hexdigest(),'new_sha256':hashlib.sha256(encoded).hexdigest()},indent=2))
if __name__=='__main__':main()
