#!/usr/bin/env python3
"""Decode OoT3D CMB material lighting/combiner config for a room, to confirm the
vertex-lighting porting model (does scene world geometry use vertex lighting, and
does the combiner multiply texture by the per-vertex color?).

Reads material flags + diffuse/ambient + the TEV combiner stages straight from the
3DS ROM's room CMB, using offsets per noclip's cmb.ts readMatsChunk.
"""
import os, sys, struct
sys.path.insert(0, "tools")
from ctr_romfs import CtrRom
import zsi as zsimod

def u8(b,o): return b[o]
def s8(b,o): return struct.unpack_from("<b",b,o)[0]
def u16(b,o): return struct.unpack_from("<H",b,o)[0]
def u32(b,o): return struct.unpack_from("<I",b,o)[0]
def f32(b,o): return struct.unpack_from("<f",b,o)[0]
def rgba8(b,o):  # big-endian RGBA8 per noclip colorNewFromRGBA8(getUint32(false))
    v=struct.unpack_from(">I",b,o)[0]; return ((v>>24)&255,(v>>16)&255,(v>>8)&255,v&255)

SRC={0x84C0:"TEX0",0x84C1:"TEX1",0x84C2:"TEX2",0x84C3:"TEX3",0x8576:"CONST",
     0x8577:"PRIMARY(vColor)",0x8578:"PREV",0x8579:"PREV_BUF",
     0x6210:"FRAG_PRI(light)",0x6211:"FRAG_SEC(spec)"}
COMB={0x1E01:"REPLACE",0x2100:"MODULATE",0x0104:"ADD",0x8574:"ADD_SIGNED",
      0x8575:"INTERP",0x84E7:"SUB",0x86AE:"DOT3_RGB",0x86AF:"DOT3_RGBA",
      0x6401:"MULT_ADD",0x6402:"ADD_MULT"}
OP={0x0300:"col",0x0301:"1-col",0x0302:"a",0x0303:"1-a",0x8580:"r",0x8581:"g",0x8582:"b"}

def find_cmb(blob):
    i=blob.find(b"cmb ")
    return i if i>=0 else None

def parse_mats(b, base):
    # base = start of 'mats' chunk
    assert b[base:base+4]==b"mats", b[base:base+4]
    count=u32(b,base+0x08)
    # material data starts at base+0x0C; stride 0x15C for ver<=6
    stride=0x15C
    matsStart=base+0x0C
    combTableOffs=matsStart+count*stride
    print(f"materials: {count}")
    for m in range(count):
        offs=matsStart+m*stride
        fragLit=u8(b,offs+0x00)
        vtxLit=u8(b,offs+0x01)
        renderLayer=u8(b,offs+0x03)
        amb=rgba8(b,offs+0xA4)
        dif=rgba8(b,offs+0xA8)
        lightCfg=u32(b,offs+0xE4)
        nComb=u32(b,offs+0x120)
        stages=[]
        idx=offs+0x124
        SCALE={1:"x1",2:"x2",4:"x4"}
        for i in range(nComb):
            ci=u16(b,idx); idx+=0x02
            co=combTableOffs+ci*0x28
            cRGB=u16(b,co+0x00); scR=u16(b,co+0x04); s0=u16(b,co+0x0C); s1=u16(b,co+0x0E); s2=u16(b,co+0x10)
            stages.append((COMB.get(cRGB,hex(cRGB)),SCALE.get(scR,scR),
                           SRC.get(s0,hex(s0)),SRC.get(s1,hex(s1)),SRC.get(s2,hex(s2))))
        print(f" mat{m}: vtxLit={vtxLit} fragLit={fragLit} layer={renderLayer} "
              f"amb={amb[:3]} dif={dif[:3]} nComb={nComb}")
        for i,st in enumerate(stages):
            print(f"     stage{i}: {st[0]:9} scaleRGB={st[1]:3} src=[{st[2]},{st[3]},{st[4]}]")

if __name__=="__main__":
    path=sys.argv[1] if len(sys.argv)>1 else "/scene/spot04_0_info.zsi"
    rom=CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    z=zsimod.Zsi(rom.read(rom.get(path)))
    # room zsi embeds the CMB; find raw 'mats'
    blob=z.data if hasattr(z,"data") else rom.read(rom.get(path))
    mi=blob.find(b"mats")
    print(f"path={path} mats@{mi}")
    parse_mats(blob, mi)
