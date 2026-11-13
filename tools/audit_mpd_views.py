#!/usr/bin/env python3
"""Audit on-device view data and sort controls without issuing playback commands."""
import argparse
import json
from pathlib import Path
import struct
import time
from urllib.parse import urlencode
from urllib.request import Request,urlopen
import zlib


def ascii_key(value):
    return value.encode('utf-8').translate(bytes.maketrans(b'ABCDEFGHIJKLMNOPQRSTUVWXYZ',b'abcdefghijklmnopqrstuvwxyz'))


def png_from_bmp(data):
    if data[:2]!=b'BM': raise ValueError('Screenshot response is not BMP')
    offset=struct.unpack_from('<I',data,10)[0]
    width,height=struct.unpack_from('<ii',data,18)
    if struct.unpack_from('<H',data,28)[0]!=24: raise ValueError('Expected 24-bit screenshot')
    stride=(width*3+3)&~3;raw=bytearray()
    for y in range(abs(height)):
        row=offset+(abs(height)-1-y if height>0 else y)*stride
        raw.append(0)
        for x in range(width):raw.extend(data[row+x*3:row+x*3+3][::-1])
    def chunk(kind,payload):
        return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(kind+payload)&0xffffffff)
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,abs(height),8,2,0,0,0))+chunk(b'IDAT',zlib.compress(bytes(raw)))+chunk(b'IEND',b'')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url',default='http://127.0.0.1:10082')
    parser.add_argument('--output',type=Path,default=Path('/tmp/mpd-view-audit'))
    parser.add_argument('--screenshots',action='store_true')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    base=args.url.rstrip('/')
    def read(path):
        with urlopen(base+path,timeout=30) as response:return response.read()
    def get(path):return json.loads(read(path))
    def post(path,body=None):
        request=Request(base+path,data=json.dumps(body).encode() if body is not None else b'',method='POST',headers={'Content-Type':'application/json'})
        with urlopen(request,timeout=30) as response:return response.read()
    def view(name,**params):return get('/api/library?'+urlencode({'view':name,'limit':128,**params}))
    def sample(name):
        first=view(name);count=first['total'];items=first['items']
        if count>len(items):items+=view(name,offset=max(len(items),count-128))['items']
        return count,items
    deadline=time.monotonic()+180
    while True:
        state=get('/api/status')
        if state.get('mpd_connected') and not state.get('mpd_syncing'):break
        if time.monotonic()>deadline:raise RuntimeError(state.get('mpd_status','MPD is unavailable'))
        print('Waiting for library:',state.get('catalog'),state.get('mpd_status'),flush=True)
        time.sleep(5)
    original=get('/api/config')
    saved={key:original[key] for key in ['sort_field','sort_desc','song_sort_field','song_sort_desc']}
    report={'firmware':state.get('elf_sha256'),'catalog':state.get('catalog'),'views':{},'sorts':[]}
    try:
        for name in ['albums','artists','songs','playlists','queue']:
            count,items=sample(name)
            report['views'][name]={'total':count,'sampled':len(items)}
            assert len(items)==min(count,256),(name,count,len(items))
            assert all('title' in item or 'name' in item for item in items),(name,'missing metadata')
            print(name,count,'entries; sample metadata valid',flush=True)
        for field in range(3):
            for desc in [False,True]:
                post('/api/config',{'sort_field':field,'sort_desc':desc,'song_sort_field':field,'song_sort_desc':desc})
                time.sleep(.7)
                _,albums=sample('albums')
                if field==0:albums=[item for item in albums if item['date_added']]
                def key(item):
                    title=ascii_key(item['title']);id=int(item['id'],16)
                    if field==1:return title,ascii_key(item['artist']),id
                    return item['date_added' if field==0 else 'year'],title,id
                values=[key(item) for item in albums]
                assert values==sorted(values,reverse=desc),('albums',field,desc)
                # Songs applies its saved sort on entering the actual page.
                post('/api/key?k=songs');time.sleep(.7)
                _,songs=sample('songs')
                if field==1:
                    values=[(ascii_key(s['title']),ascii_key(s['artist']),int(s['id'],16)) for s in songs]
                    assert values==sorted(values,reverse=desc),('songs',field,desc)
                elif field==2:
                    years=[s['year'] for s in songs if s['year']]
                    assert years==sorted(years,reverse=desc),('songs',field,desc)
                report['sorts'].append({'field':field,'descending':desc,'album_sample':len(albums),'song_sample':len(songs)})
                print('sort',field,'descending' if desc else 'ascending','verified',flush=True)
        count,albums=sample('albums')
        report['album_details']=[]
        for album in albums[:3]:
            tracks=view('album_tracks',parent=album['id'])
            assert tracks['total']==album['tracks'],('album count',album['title'])
            numbers=[(track['disc'] or 1,track['track']) for track in tracks['items']]
            assert numbers==sorted(numbers),('album track order',album['title'])
            assert all(track['album_id']==album['id'] for track in tracks['items'])
            report['album_details'].append({'title':album['title'],'tracks':tracks['total']})
        _,artists=sample('artists')
        names=[ascii_key(item['name']) for item in artists]
        assert names==sorted(names),'Artist list is not alphabetical'
        report['artist_details']=[]
        for artist in artists[:3]:
            tracks=view('artist_tracks',parent=artist['id'])
            assert tracks['total']==artist['songs'],('artist count',artist['name'])
            assert all(track['artist_id']==artist['id'] for track in tracks['items'])
            report['artist_details'].append({'name':artist['name'],'tracks':tracks['total']})
        _,playlists=sample('playlists')
        report['playlist_details']=[]
        for playlist in playlists:
            tracks=view('playlist_tracks',index=playlist['index'])
            assert tracks['total']==playlist['tracks'],('playlist count',playlist['name'])
            report['playlist_details'].append({'name':playlist['name'],'tracks':tracks['total']})
        if args.screenshots:
            for name in ['nowplaying','albums','artists','songs','playlists','settings']:
                post('/api/key?'+urlencode({'k':name}));time.sleep(.8)
                image=png_from_bmp(read('/api/screenshot.bmp'))
                (args.output/(name+'.png')).write_bytes(image)
                print('captured',name,flush=True)
        report['final_status']=get('/api/status')['mpd_status']
    finally:
        post('/api/config',saved)
        post('/api/key?k=nowplaying')
        print('Original sort preferences restored',flush=True)
    (args.output/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2))
    print('View audit passed:',args.output/'report.json',flush=True)

if __name__=='__main__':main()
