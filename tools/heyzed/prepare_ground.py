"""Author vector road markings from explicit tile registrations. No game images are read."""
from pathlib import Path
import json

ROOT=Path(__file__).resolve().parent


def quad(x,y,w,h):
    return [[x,y],[x+w,y],[x+w,y+h],[x,y+h]]


def main():
    # Tile variants name the four tile edges and their paired junctions.
    variants=[('W',),('W','N'),('N',),('N','E'),('E',),('E','S'),('S',),('S','W')]
    overlays={}
    width=.10
    for index in range(50):
        if index==39: continue
        if index>=32:
            overlays[f'street_trafficlines_01_{index}']=dict(material='PaintHidden',patches=[quad(0,0,1,1)],status='unregistered glyph')
            continue
        patches=[]
        dashed=index//8 in (1,3)
        start,length=(.2,.6) if dashed else (0,1)
        for edge in variants[index%8]:
            inset=.03
            if edge=='W': patches.append(quad(inset,start,width,length))
            elif edge=='E': patches.append(quad(1-inset-width,start,width,length))
            elif edge=='N': patches.append(quad(start,inset,length,width))
            else: patches.append(quad(start,1-inset-width,length,width))
        overlays[f'street_trafficlines_01_{index}']=dict(material='PaintYellow' if index>=16 else 'PaintWhite',patches=patches)
    (ROOT/'ground-shapes.json').write_text(json.dumps(dict(schema=2,interpretation='Authored vector edge markings. No source images, alpha masks or projected sprite pixels. Unregistered glyphs are hidden.',overlays=overlays),separators=(',',':')),encoding='utf-8')
    print(f'Authored {len(overlays)} vector registrations; 17 unsupported glyphs hidden')


if __name__=='__main__': main()
