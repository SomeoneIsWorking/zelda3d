#!/usr/bin/env python3
# Parse N64 OoT object SKELETONS offline from the ROM (so we don't have to drive every scene).
# Pipeline: ROM dmadata -> per-object bytes (yaz0-decompress) -> SkeletonHeader at the offset the
# object XML gives -> walk limb pointer table -> StandardLimb {jointPos, child, sibling}.
# Validates against the in-game ZELDA3D_SKELDUMP. Big-endian N64 data.
import struct, sys, os, re, glob
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# N64 OoT USA/NTSC v1.0 ROM. Provide it via the ZELDA3D_OOT_ROM env var (see the gitignored .env)
# or drop the .z64 into the repo root as oot_ntsc_10.z64. Never commit the ROM (copyrighted).
ROM = os.environ.get("ZELDA3D_OOT_ROM") or os.path.join(REPO, "oot_ntsc_10.z64")
FILELIST = os.path.join(REPO, "Shipwright/soh/assets/extractor/filelists/ntsc_oot.txt")
# XML offsets are NOT version-stable: anim offsets differ between PAL and NTSC for some objects
# (e.g. object_du gDaruniaIdleAnim = 0x6EB0 PAL vs 0x74B0 NTSC). The ROM above is USA/NTSC, so the
# XML version MUST be an NTSC one or the frameCount reads land on garbage bytes (was 58/913 wrong
# with the PAL dir -> bad anim<->CSAB matches like Darunia's). N64_NTSC_10 matches this USA ROM.
XMLDIR = os.path.join(REPO, "Shipwright/soh/assets/xml/N64_NTSC_10/objects")

def be16(b,o): return struct.unpack_from(">h",b,o)[0]
def beu16(b,o): return struct.unpack_from(">H",b,o)[0]
def beu32(b,o): return struct.unpack_from(">I",b,o)[0]

def yaz0(data):
    if data[:4] != b"Yaz0": return data
    size = beu32(data,4); out=bytearray(); src=16
    while len(out)<size:
        gb=data[src]; src+=1
        for i in range(8):
            if len(out)>=size: break
            if gb & (0x80>>i):
                out.append(data[src]); src+=1
            else:
                b0,b1=data[src],data[src+1]; src+=2
                dist=((b0&0xF)<<8)|b1; n=b0>>4
                if n==0: n=data[src]+0x12; src+=1
                else: n+=2
                start=len(out)-dist-1
                for k in range(n): out.append(out[start+k])
    return bytes(out)

def find_dmadata(rom):
    # dmadata entry 0 = makerom {0,0x1060,0,0}; scan 16-byte-aligned for that signature.
    for off in range(0x1000, 0x40000, 16):
        if beu32(rom,off)==0 and beu32(rom,off+4)==0x1060 and beu32(rom,off+8)==0 and beu32(rom,off+12)==0:
            # sanity: entry 1 vromStart==0x1060
            if beu32(rom,off+16)==0x1060: return off
    raise RuntimeError("dmadata not found")

def load_objects(rom, names_wanted):
    dma=find_dmadata(rom)
    files=[l.strip() for l in open(FILELIST) if l.strip()]
    out={}
    for idx,name in enumerate(files):
        if name not in names_wanted: continue
        e=dma+idx*16
        vs,ve,rs,re_=beu32(rom,e),beu32(rom,e+4),beu32(rom,e+8),beu32(rom,e+12)
        if rs==0xFFFFFFFF: continue
        raw = rom[rs:re_] if re_ else rom[rs:rs+(ve-vs)]
        out[name]=yaz0(raw)
    return out, dma

def parse_skeleton(obj, skel_off):
    # FlexSkeletonHeader: u32 segment(limb table), u8 limbCount, _, u8 dListCount
    seg_ptr=beu32(obj,skel_off); limb_count=obj[skel_off+4]
    base=seg_ptr & 0xFFFFFF
    limbs=[]
    for i in range(limb_count):
        lp=beu32(obj, base+i*4) & 0xFFFFFF
        jx,jy,jz=be16(obj,lp),be16(obj,lp+2),be16(obj,lp+4)
        child,sibling=obj[lp+6],obj[lp+7]
        limbs.append(dict(id=i,jp=(jx,jy,jz),child=child,sibling=sibling))
    return limb_count, limbs

def skel_offset_from_xml(name):
    f=os.path.join(XMLDIR, name+".xml")
    if not os.path.exists(f): return None
    m=re.search(r'<Skeleton[^>]*Type="Flex"[^>]*Offset="(0x[0-9A-Fa-f]+)"', open(f).read())
    if not m:
        m=re.search(r'<Skeleton[^>]*Offset="(0x[0-9A-Fa-f]+)"', open(f).read())
    return int(m.group(1),16) if m else None

if __name__=="__main__":
    rom=open(ROM,"rb").read()
    name=sys.argv[1] if len(sys.argv)>1 else "object_boj"
    objs,dma=load_objects(rom,{name})
    print("dmadata @ 0x%X"%dma)
    if name not in objs: sys.exit("object not found: "+name)
    so=skel_offset_from_xml(name)
    print("%s skel offset=0x%X objbytes=%d"%(name,so,len(objs[name])))
    lc,limbs=parse_skeleton(objs[name],so)
    print("limbCount=%d"%lc)
    for l in limbs:
        print("  limb=%d jointPos=%s child=%d sibling=%d"%(l['id'],l['jp'],l['child'],l['sibling']))
