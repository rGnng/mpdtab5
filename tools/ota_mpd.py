#!/usr/bin/env python3
"""Upload a Tab5 application and verify the running ELF hash after reboot."""
import argparse
import hashlib
import json
from pathlib import Path
import time
from urllib.error import URLError
from urllib.request import Request, urlopen

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url',default='http://127.0.0.1:10082')
    parser.add_argument('--image',type=Path,default=ROOT/'firmware/build/mpdtab5.bin')
    parser.add_argument('--elf',type=Path,default=ROOT/'firmware/build/mpdtab5.elf')
    args=parser.parse_args()
    image=args.image.read_bytes()
    expected=hashlib.sha256(args.elf.read_bytes()).hexdigest()
    base=args.url.rstrip('/')
    print(f'Uploading {len(image):,} bytes to {base}/api/ota',flush=True)
    request=Request(base+'/api/ota',data=image,method='POST',headers={'Content-Type':'application/octet-stream'})
    with urlopen(request,timeout=120) as response:
        print(response.read().decode(),flush=True)
    deadline=time.monotonic()+120
    while time.monotonic()<deadline:
        time.sleep(2)
        try:
            with urlopen(base+'/api/status',timeout=5) as response:
                state=json.load(response)
            if state.get('project')=='mpdtab5' and state.get('elf_sha256')==expected:
                print(json.dumps({key:state.get(key) for key in ['project','firmware','elf_sha256','uptime_s','ip','sd_ready','mpd_host','mpd_status','free_psram']},indent=2))
                return 0
        except (URLError,TimeoutError,OSError,ValueError):
            pass
    parser.error('Firmware upload succeeded, but the running ELF hash was not verified within 120 seconds')

if __name__=='__main__':
    raise SystemExit(main())
