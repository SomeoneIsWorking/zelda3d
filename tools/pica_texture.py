#!/usr/bin/env python3
"""Decode PICA200 (3DS) textures to RGBA8. Port of noclip oot3d/pica_texture.ts.

3DS textures are stored in 8x8 tiles with Morton-order swizzle inside each tile;
ETC1/ETC1A4 are 4x4-block compressed. glFormat = (dataType<<16)|formatConstant.
"""
from __future__ import annotations
import struct

# glFormat (u32) -> internal format name
GLFMT = {
    0x0000675A:"ETC1", 0x0000675B:"ETC1A4",
    0x14016754:"RGB8", 0x14016752:"RGBA8",
    0x80336752:"RGBA4444", 0x80346752:"RGBA5551", 0x83636754:"RGB565",
    0x14016756:"A8", 0x67616756:"A4",
    0x14016757:"L8", 0x67616757:"L4",
    0x14016758:"LA8", 0x67606758:"LA4",
    0x14016759:"HILO8",
}

def e4(n): return (n<<4)|n
def e5(n): return (n<<3)|(n>>2)
def e6(n): return (n<<2)|(n>>4)
def _clamp(v): return 0 if v<0 else 255 if v>255 else v

def _morton7(n):
    return ((n>>2)&0x04)|((n>>1)&0x02)|(n&0x01)

def _tiled(width,height,decode):
    """decode(srcindex)-> (r,g,b,a); returns RGBA8 bytearray. srcindex increments per pixel in tile order."""
    px=bytearray(width*height*4)
    si=0
    for yy in range(0,height,8):
        for xx in range(0,width,8):
            for i in range(0x40):
                x=_morton7(i); y=_morton7(i>>1)
                d=((yy+y)*width+(xx+x))*4
                r,g,b,a=decode(si); si+=1
                px[d]=r; px[d+1]=g; px[d+2]=b; px[d+3]=a
    return px

# ---- ETC1 ----
_INTENS=[[2,8,-2,-8],[5,17,-5,-17],[9,29,-9,-29],[13,42,-13,-42],
         [18,60,-18,-60],[24,80,-24,-80],[33,106,-33,-106],[47,183,-47,-183]]
def _s3(n): return n - 8 if (n & 4) else n  # sign-extend 3-bit (n=4..7 -> -4..-1)

def _etc1_color(dst,w1,w2,dstOffs,stride):
    diff=(w1&0x02)!=0; flip=(w1&0x01)!=0
    it1=_INTENS[(w1>>5)&7]; it2=_INTENS[(w1>>2)&7]
    def colors(r,g,b,it):
        return [(_clamp(r+it[i]),_clamp(g+it[i]),_clamp(b+it[i])) for i in range(4)]
    if diff:
        r1a=(w1>>27)&0x1F; r2d=_s3((w1>>24)&7)
        g1a=(w1>>19)&0x1F; g2d=_s3((w1>>16)&7)
        b1a=(w1>>11)&0x1F; b2d=_s3((w1>>8)&7)
        c1=colors(e5(r1a),e5(g1a),e5(b1a),it1)
        c2=colors(e5(r1a+r2d),e5(g1a+g2d),e5(b1a+b2d),it2)
    else:
        c1=colors(e4((w1>>28)&0xF),e4((w1>>20)&0xF),e4((w1>>12)&0xF),it1)
        c2=colors(e4((w1>>24)&0xF),e4((w1>>16)&0xF),e4((w1>>8)&0xF),it2)
    for i in range(16):
        lsb=(w2>>i)&1; msb=(w2>>(16+i))&1; lookup=(msb<<1)|lsb
        y=i&3; x=i>>2
        di=dstOffs+((y*stride)+x)*4
        which = (x&2) if not flip else (y&2)
        col=c2 if which else c1
        dst[di]=col[lookup][0]; dst[di+1]=col[lookup][1]; dst[di+2]=col[lookup][2]

def _etc1_alpha(dst,a1,a2,dstOffs,stride):
    for ax in range(2):
        for ay in range(4):
            dst[dstOffs+((ay*stride)+ax)*4+3]=e4(a2&0xF); a2>>=4
    for ax in range(2,4):
        for ay in range(4):
            dst[dstOffs+((ay*stride)+ax)*4+3]=e4(a1&0xF); a1>>=4

def _decode_etc1(width,height,data,alpha):
    px=bytearray(width*height*4); stride=width; offs=0
    for yy in range(0,height,8):
        for xx in range(0,width,8):
            for y in range(0,8,4):
                for x in range(0,8,4):
                    dstOffs=((yy+y)*stride+(xx+x))*4
                    if alpha:
                        a2=struct.unpack_from("<I",data,offs)[0]; a1=struct.unpack_from("<I",data,offs+4)[0]; offs+=8
                    else:
                        a1=a2=0xFFFFFFFF
                    _etc1_alpha(px,a1,a2,dstOffs,stride)
                    w2=struct.unpack_from("<I",data,offs)[0]; w1=struct.unpack_from("<I",data,offs+4)[0]; offs+=8
                    _etc1_color(px,w1,w2,dstOffs,stride)
    return px

def decode(glformat:int, width:int, height:int, data:bytes)->bytearray:
    name=GLFMT.get(glformat)
    if name is None:
        raise ValueError("unknown glformat 0x%08x"%glformat)
    if name=="ETC1":   return _decode_etc1(width,height,data,False)
    if name=="ETC1A4": return _decode_etc1(width,height,data,True)
    d=data
    if name=="RGBA8":
        def f(i):
            p=struct.unpack_from("<I",d,i*4)[0]; return ((p>>24)&0xFF,(p>>16)&0xFF,(p>>8)&0xFF,p&0xFF)
        return _tiled(width,height,f)
    if name=="RGB8":
        def f(i):
            b0,b1,b2=d[i*3],d[i*3+1],d[i*3+2]; return (b2,b1,b0,0xFF)
        return _tiled(width,height,f)
    if name=="RGB565":
        def f(i):
            p=struct.unpack_from("<H",d,i*2)[0]; return (e5((p>>11)&0x1F),e6((p>>5)&0x3F),e5(p&0x1F),0xFF)
        return _tiled(width,height,f)
    if name=="RGBA5551":
        def f(i):
            p=struct.unpack_from("<H",d,i*2)[0]; return (e5((p>>11)&0x1F),e5((p>>6)&0x1F),e5((p>>1)&0x1F),0xFF if p&1 else 0)
        return _tiled(width,height,f)
    if name=="RGBA4444":
        def f(i):
            p=struct.unpack_from("<H",d,i*2)[0]; return (e4((p>>12)&0xF),e4((p>>8)&0xF),e4((p>>4)&0xF),e4(p&0xF))
        return _tiled(width,height,f)
    if name=="LA8":
        def f(i):
            a=d[i*2]; L=d[i*2+1]; return (L,L,L,a)
        return _tiled(width,height,f)
    if name=="HILO8":
        def f(i):
            hi=d[i*2+1]; lo=d[i*2]; return (hi,lo,0xFF,0xFF)
        return _tiled(width,height,f)
    if name=="L8":
        # Luminance-only: no stored alpha, so alpha is OPAQUE (matches the authoritative
        # C++ decoder, Shipwright/cmb3d/asset/pica_texture.cpp GF_L8 — aliasing L into
        # alpha was the title-screen star-brightness bug fixed there).
        def f(i):
            L=d[i]; return (L,L,L,0xFF)
        return _tiled(width,height,f)
    if name=="A8":
        def f(i):
            return (0xFF,0xFF,0xFF,d[i])
        return _tiled(width,height,f)
    if name=="L4":
        def f(i):
            # same luminance-only convention as L8: alpha opaque, not aliased to L.
            p=d[i>>1]; n=(p>>4) if (i&1) else (p&0xF); L=e4(n); return (L,L,L,0xFF)
        return _tiled(width,height,f)
    if name=="A4":
        def f(i):
            p=d[i>>1]; n=(p>>4) if (i&1) else (p&0xF); return (0xFF,0xFF,0xFF,e4(n))
        return _tiled(width,height,f)
    raise ValueError("decoder not implemented for "+name)


def decode_cmb_texture(cmb, tex):
    """Decode a Texture from a parsed Cmb into RGBA8 bytes."""
    base = cmb.texdata_ptr + tex.data_offset
    raw = cmb.data[base: base+tex.data_len]
    return tex.width, tex.height, decode(tex.gl_format, tex.width, tex.height, raw)


if __name__=="__main__":
    import sys
    from tools.cmb import Cmb
    from PIL import Image
    path=sys.argv[1] if len(sys.argv)>1 else "scratch/extract/tubo2_model.cmb"
    c=Cmb(open(path,"rb").read())
    for i,t in enumerate(c.textures):
        name=GLFMT.get(t.gl_format,"0x%08x"%t.gl_format)
        w,h,px=decode_cmb_texture(c,t)
        out="scratch/screenshots/tex_%s_%d_%s.png"%(c.name,i,t.name)
        Image.frombytes("RGBA",(w,h),bytes(px)).save(out)
        print(f"  tex{i} {t.name:18} {w}x{h} {name:8} -> {out}")
