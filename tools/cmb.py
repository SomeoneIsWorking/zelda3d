#!/usr/bin/env python3
"""Parser for OoT3D/MM3D CMB models (geometry + skeleton + material/texture refs).

Indexing model (verified against noclip.website OcarinaOfTime3D/cmb.ts):
  - VATR holds one raw byte-buffer per attribute (position/normal/color/uv/...).
  - Each SEPD attribute has: start (byte offset into that attr's VATR buffer),
    scale (multiplier for fixed-point data), dataType (GL type), mode (array/const).
  - PRM triangle indices are GLOBAL: for index `idx`, attribute value =
    VATR[attr][ sepd.attr.start + idx * components * sizeof(dataType) ] * scale.

References: CloudModding 3D:CMB_format, noclip OcarinaOfTime3D/cmb.ts.
"""
from __future__ import annotations
import struct, math
from dataclasses import dataclass, field

# ---- tiny 4x4 matrix helpers (column-vector convention, M @ [x,y,z,1]) ----
def _mat_id(): return [[1.0 if i==j else 0.0 for j in range(4)] for i in range(4)]
def _mat_mul(A,B):
    return [[sum(A[i][k]*B[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
def _mat_T(x,y,z): return [[1,0,0,x],[0,1,0,y],[0,0,1,z],[0,0,0,1]]
def _mat_S(x,y,z): return [[x,0,0,0],[0,y,0,0],[0,0,z,0],[0,0,0,1]]
def _mat_Rx(a):
    c,s=math.cos(a),math.sin(a); return [[1,0,0,0],[0,c,-s,0],[0,s,c,0],[0,0,0,1]]
def _mat_Ry(a):
    c,s=math.cos(a),math.sin(a); return [[c,0,s,0],[0,1,0,0],[-s,0,c,0],[0,0,0,1]]
def _mat_Rz(a):
    c,s=math.cos(a),math.sin(a); return [[c,-s,0,0],[s,c,0,0],[0,0,1,0],[0,0,0,1]]
def _mat_apply_pos(M,p):
    return tuple(sum(M[i][k]*(p[k] if k<3 else 1.0) for k in range(4)) for i in range(3))
def _mat_apply_dir(M,v):   # rotation+scale only, no translation (for normals)
    return tuple(sum(M[i][k]*v[k] for k in range(3)) for i in range(3))

def _u8(b,o):  return b[o]
def _s16(b,o): return struct.unpack_from("<h",b,o)[0]
def _u16(b,o): return struct.unpack_from("<H",b,o)[0]
def _u32(b,o): return struct.unpack_from("<I",b,o)[0]
def _f32(b,o): return struct.unpack_from("<f",b,o)[0]
def _vec(b,o,n): return struct.unpack_from("<%df"%n,b,o)

# GL data types
DT_BYTE,DT_UBYTE,DT_SHORT,DT_USHORT,DT_INT,DT_UINT,DT_FLOAT = \
    0x1400,0x1401,0x1402,0x1403,0x1404,0x1405,0x1406
DT_SIZE = {DT_BYTE:1,DT_UBYTE:1,DT_SHORT:2,DT_USHORT:2,DT_INT:4,DT_UINT:4,DT_FLOAT:4}
DT_FMT  = {DT_BYTE:"b",DT_UBYTE:"B",DT_SHORT:"h",DT_USHORT:"H",DT_INT:"i",DT_UINT:"I",DT_FLOAT:"f"}

MODE_ARRAY, MODE_CONSTANT = 0, 1   # Sepd attribute mode

@dataclass
class Bone:
    id:int; parent:int; scale:tuple; rot:tuple; trans:tuple

@dataclass
class SepdAttr:
    start:int; scale:float; data_type:int; mode:int; constant:tuple

@dataclass
class Prm:
    index_type:int; count:int; first:int   # first = first index (in elements)

@dataclass
class Prms:
    skinning_mode:int; bone_table:list; prms:list  # prms: list[Prm]

@dataclass
class Sepd:
    attrs:dict                 # name -> SepdAttr
    prim_count:int
    bone_dimension:int
    prms:list                  # list[Prms]

@dataclass
class Mesh:
    sepd_index:int; material_index:int; mesh_id:int

# GL texture wrap modes (CMB stores the raw GL enum).
WRAP_CLAMP, WRAP_REPEAT, WRAP_CLAMP_EDGE, WRAP_MIRROR = 0x2900, 0x2901, 0x812F, 0x8370

@dataclass
class Material:
    index:int
    tex0_idx:int          # texture index of binding 0 (-1 if none)
    min_filter:int; mag_filter:int # GL sampler enums for binding 0
    wrap_s:int; wrap_t:int # GL wrap enums for binding 0
    scale_s:float; scale_t:float
    trans_s:float; trans_t:float
    rot:float
    cull:int              # 0=none 1=back 2=front 3=both (raw flags)
    alpha_test:bool
    alpha_ref:float
    # Blend state. The CMB stores GL-ES enum values directly (0x0302 GL_SRC_ALPHA,
    # 0x0001 GL_ONE, 0x8006 GL_FUNC_ADD), usable verbatim in desktop GL. blend_enable
    # off => opaque (alpha-test only). Additive light-shaft mats have dst_rgb=GL_ONE.
    blend_enable:bool=False
    blend_src_rgb:int=0x0302; blend_dst_rgb:int=0x0303
    blend_src_a:int=0x0001;   blend_dst_a:int=0x0000
    blend_eq_rgb:int=0x8006;  blend_eq_a:int=0x8006
    blend_color:tuple=(0.0,0.0,0.0,1.0)
    depth_write:bool=True

@dataclass
class Texture:
    name:str; width:int; height:int; fmt:int; data_type:int; etc1:bool; data_offset:int; data_len:int
    @property
    def gl_format(self):   # (dataType<<16)|formatConstant -> TextureFormatGL key
        return (self.data_type<<16)|self.fmt

# attribute name -> component count. OoT3D (v6) has NO tangent slot; MM3D (v7) does.
ATTRS_OOT3D = [("position",3),("normal",3),("color",4),
               ("texCoord0",2),("texCoord1",2),("texCoord2",2),
               ("boneIndices",0),("boneWeights",0)]
ATTRS_MM3D  = [("position",3),("normal",3),("tangent",3),("color",4),
               ("texCoord0",2),("texCoord1",2),("texCoord2",2),
               ("boneIndices",0),("boneWeights",0)]


class Cmb:
    def __init__(self, data: bytes):
        self.data = b = data
        assert b[0:4]==b"cmb ", "not a CMB"
        self.version = _u32(b,0x08)
        self.name = b[0x10:0x20].split(b"\x00")[0].decode("ascii","replace")
        self.index_count = _u32(b,0x20)
        self.skl_ptr  = _u32(b,0x24)
        # MM3D (v>=7) inserts a qtrs pointer after skl, shifting the remaining
        # chunk pointers by four bytes. This is the same version gate as the
        # shipping C++ parser (Shipwright/cmb3d/asset/cmb.cpp).
        d = 4 if self.version >= 7 else 0
        self.mats_ptr = _u32(b,0x28+d)
        self.tex_ptr  = _u32(b,0x2C+d)
        self.sklm_ptr = _u32(b,0x30+d)
        self.luts_ptr = _u32(b,0x34+d)
        self.vatr_ptr = _u32(b,0x38+d)
        self.idx_ptr  = _u32(b,0x3C+d)
        self.texdata_ptr = _u32(b,0x40+d)
        self.attrs_def = ATTRS_MM3D if self.version>=7 else ATTRS_OOT3D
        self._parse_skl()
        self._compute_bone_matrices()
        self._parse_mats()
        self._parse_vatr()
        self._parse_tex()
        self._parse_sklm()

    # ---- SKL ----
    def _parse_skl(self):
        b=self.data; p=self.skl_ptr
        assert b[p:p+4]==b"skl ", "bad skl"
        n=_u32(b,p+8)
        self.bones=[]
        o=p+0x10
        bone_stride = 0x2C if self.version >= 7 else 0x28
        for _ in range(n):
            bid=_u8(b,o); parent=_s16(b,o+2)
            scale=_vec(b,o+4,3); rot=_vec(b,o+0x10,3); trans=_vec(b,o+0x1C,3)
            self.bones.append(Bone(bid,parent,scale,rot,trans))
            o += bone_stride

    # ---- bind-pose bone matrices ----
    def _compute_bone_matrices(self):
        """World (bind-pose) matrix per bone, so rigidly-skinned meshes stored in
        bone-local space land in the right place. Local = T * Rz * Ry * Rx * S
        (CTR/Grezzo ZYX euler); world = parent_world * local. The single-bone pot
        never needed this (bone 0 = identity); multi-bone props (chest lid, etc.)
        do — without it bone-local meshes render scrambled. Verified on tr_box:
        the lid (bone 2) then sits exactly atop the base."""
        by_id = {b.id: b for b in self.bones}
        self.bone_matrix = {}
        def world(bid):
            if bid in self.bone_matrix: return self.bone_matrix[bid]
            b = by_id[bid]
            L = _mat_mul(_mat_T(*b.trans),
                         _mat_mul(_mat_mul(_mat_Rz(b.rot[2]), _mat_Ry(b.rot[1])),
                                  _mat_Rx(b.rot[0])))
            L = _mat_mul(L, _mat_S(*b.scale))
            W = L if b.parent < 0 else _mat_mul(world(b.parent), L)
            self.bone_matrix[bid] = W
            return W
        for b in self.bones:
            world(b.id)

    # ---- MATS ----
    def _parse_mats(self):
        """Parse the material table: per-material primary texture binding + UV
        coordinator + alpha test. Verified against noclip readMatsChunk (Ocarina
        layout, material stride 0x15C; >Ocarina adds 0x10)."""
        b=self.data; p=self.mats_ptr
        self.materials=[]
        if p==0 or b[p:p+4]!=b"mats": return
        n=_u32(b,p+8)
        stride = 0x15C if self.version<=6 else 0x16C
        o=p+0x0C
        for i in range(n):
            cull=_u8(b,o+4)
            # binding 0 at o+0x10: textureIdx(s16), min/mag(u16)@+4/+6,
            # wrapS(u16)@+8, wrapT@+0xA
            bo=o+0x10
            tex0=_s16(b,bo)
            min_filter=_u16(b,bo+4); mag_filter=_u16(b,bo+6)
            wrap_s=_u16(b,bo+8); wrap_t=_u16(b,bo+0x0A)
            # coordinator 0 at o+0x58: scaleS/T, transS/T, rot (f32)
            co=o+0x58
            sS,sT,tS,tT,rot=struct.unpack_from("<5f",b,co+4)
            alpha_en=_u8(b,o+0x130); alpha_ref=_u8(b,o+0x131)/255.0
            # Blend state (GL-ES enums, used verbatim). Offsets per noclip readMatsChunk
            # (Ocarina v6); verified against real room materials.
            depth_w=_u8(b,o+0x135); blend_en=_u8(b,o+0x138)
            bsr=_u16(b,o+0x13C); bdr=_u16(b,o+0x13E); ber=_u16(b,o+0x140)
            bsa=_u16(b,o+0x144); bda=_u16(b,o+0x146); bea=_u16(b,o+0x148)
            bc=struct.unpack_from("<4f",b,o+0x14C)
            self.materials.append(Material(i,tex0,min_filter,mag_filter,wrap_s,wrap_t,sS,sT,tS,tT,rot,
                                           cull,bool(alpha_en),alpha_ref,
                                           bool(blend_en),bsr,bdr,bsa,bda,ber,bea,bc,bool(depth_w)))
            o+=stride

    def material_texture(self, mat_index:int)->int:
        """Texture index used by a material's primary binding (0 if unknown)."""
        if 0<=mat_index<len(self.materials):
            t=self.materials[mat_index].tex0_idx
            return t if t>=0 else 0
        return 0

    # ---- VATR ----
    def _parse_vatr(self):
        b=self.data; p=self.vatr_ptr
        assert b[p:p+4]==b"vatr", "bad vatr"
        self.max_index=_u32(b,p+8)
        self.vatr={}   # name -> (abs_offset, size)
        o=p+0x0C
        for name,_ in self.attrs_def:
            size=_u32(b,o); off=_u32(b,o+4)   # NOTE: size first, then offset
            self.vatr[name]=(self.vatr_ptr+off, size)
            o+=8

    # ---- TEX ----
    def _parse_tex(self):
        self.textures=[]
        if self.tex_ptr==0: return
        b=self.data; p=self.tex_ptr
        if b[p:p+4]!=b"tex ": return
        n=_u32(b,p+8); o=p+0x0C
        for _ in range(n):
            dlen=_u32(b,o); etc1=_u8(b,o+6); w=_u16(b,o+8); h=_u16(b,o+0x0A)
            fmt=_u16(b,o+0x0C); dtype=_u16(b,o+0x0E); doff=_u32(b,o+0x10)
            nm=b[o+0x14:o+0x24].split(b"\x00")[0].decode("ascii","replace")
            self.textures.append(Texture(nm,w,h,fmt,dtype,bool(etc1),doff,dlen))
            o+=0x24

    # ---- SKLM / SHP / SEPD / PRMS / PRM ----
    def _parse_sklm(self):
        b=self.data; p=self.sklm_ptr
        assert b[p:p+4]==b"sklm", "bad sklm"
        mshs_off=p+_u32(b,p+0x08)
        shp_off =p+_u32(b,p+0x0C)
        # MSHS: meshes
        assert b[mshs_off:mshs_off+4]==b"mshs"
        mesh_count=_u32(b,mshs_off+8)
        self.meshes=[]
        mo=mshs_off+0x10
        mesh_stride = 0x0C if self.version >= 7 else 0x04
        for _ in range(mesh_count):
            sepd_i=_u16(b,mo); mat_i=_u8(b,mo+2); mid=_u8(b,mo+3)
            self.meshes.append(Mesh(sepd_i,mat_i,mid))
            mo += mesh_stride
        # SHP: shapes -> SEPDs
        assert b[shp_off:shp_off+4]==b"shp ", "bad shp"
        sepd_count=_u32(b,shp_off+8)
        sepd_ptrs=[_u16(b,shp_off+0x10+2*i) for i in range(sepd_count)]
        self.sepds=[self._parse_sepd(shp_off+ptr) for ptr in sepd_ptrs]

    def _parse_sepd(self, p):
        b=self.data
        assert b[p:p+4]==b"sepd", "bad sepd @0x%x"%p
        prim_count=_u16(b,p+8)
        attrs={}
        o=p+0x24
        for name,_ in self.attrs_def:
            start=_u32(b,o); scale=_f32(b,o+4); dt=_u16(b,o+8); mode=_u16(b,o+0x0A)
            const=_vec(b,o+0x0C,4)
            attrs[name]=SepdAttr(start,scale,dt,mode,const)
            o+=0x1C   # VertexList = offset+scale+dt+mode + constant vec4(16) = 0x1C
        bone_dim=_u16(b,o); o+=4   # bone dimension + autogen flags
        # PRMS pointers (u16[], relative to sepd) at o
        prms_ptrs=[_u16(b,o+2*i) for i in range(prim_count)]
        prms=[self._parse_prms(p+ptr) for ptr in prms_ptrs]
        return Sepd(attrs,prim_count,bone_dim,prms)

    def _parse_prms(self, p):
        b=self.data
        assert b[p:p+4]==b"prms", "bad prms @0x%x"%p
        skin=_u16(b,p+0x0C); bt_count=_u16(b,p+0x0E)
        bt_off=p+_u32(b,p+0x10); prm_off=p+_u32(b,p+0x14)
        bone_table=[_u16(b,bt_off+2*i) for i in range(bt_count)]
        prm=self._parse_prm(prm_off)
        return Prms(skin,bone_table,[prm])

    def _parse_prm(self, p):
        b=self.data
        assert b[p:p+4]==b"prm ", "bad prm @0x%x"%p
        idx_type=_u16(b,p+0x10); count=_u16(b,p+0x14); first=_u16(b,p+0x16)
        return Prm(idx_type,count,first)

    # ---- geometry assembly ----
    def read_attr(self, attr:SepdAttr, name:str, idx:int, comps:int):
        if attr.mode==MODE_CONSTANT:
            return attr.constant[:comps]
        base,_=self.vatr[name]
        sz=DT_SIZE[attr.data_type]; fmt="<%d%s"%(comps,DT_FMT[attr.data_type])
        off=base + attr.start + idx*comps*sz
        vals=struct.unpack_from(fmt,self.data,off)
        return tuple(v*attr.scale for v in vals)

    def triangles(self):
        """Yield (sepd_index, material_index, [ (idx,pos,normal,uv0), x3 ]) per triangle.

        Bind-pose skinning. CMB vertices are stored in BONE-LOCAL space; each is
        placed by its bone's world (bind) matrix:
          - bone_dimension==1 (rigid): the whole prms is bound to bone_table[0];
            transform every vertex by that one matrix (verified on pot / tr_box).
          - bone_dimension>1 (smooth): vertices are stored in MODEL space, with the
            per-vertex bone indices + weights used only for animation (the runtime
            applies bone_current * bone_bind_inverse, which is IDENTITY at bind pose).
            So a static bind-pose render uses the RAW model-space positions with NO
            per-bone transform. (Applying either a single bone's or the root bone's
            world matrix scrambles / mis-orients the mesh — both tried on hintstone +
            geldwoman; raw model space is coherent and Y-up for both.)

        NOTE (orientation): character rest meshes (e.g. geldwoman) come out Y-up but
        HEAD-DOWN in model space — the in-game skeleton root reorients them. The
        bind-pose render reflects raw model space; final upright placement is an
        in-game / integration concern, not a conversion bug.
        """
        b=self.data
        for mesh in self.meshes:
            sepd=self.sepds[mesh.sepd_index]
            bd=sepd.bone_dimension
            has_normal = sepd.attrs["normal"].mode in (MODE_ARRAY, MODE_CONSTANT)
            for prms in sepd.prms:
                prm=prms.prms[0]
                bt=prms.bone_table or [0]
                # bone_dimension>1 => verts are MODEL space (smooth), regardless of whether the
                # boneIndices/boneWeights are ARRAY (per-vertex) or CONSTANT (one set for all verts,
                # e.g. a head on a 2-bone neck/head chain). Requiring ARRAY mis-classed those as
                # rigid and double-applied the bind matrix (the Kokiri "floating head"). Match cmb.cpp.
                smooth = bd>1
                # RIGID_SKINNING (skinning_mode==1): bd==1 but each vertex has its OWN bone index
                # (boneIndices, local into bone_table) and is in THAT bone's local space. Binding
                # all verts to bt[0] scatters the rig (stalchild #109) -> resolve per vertex below.
                bi=sepd.attrs["boneIndices"]
                rigid_pv = (not smooth) and prms.skinning_mode==1 and bi.mode==MODE_ARRAY and len(bt)>1
                # rigid -> place by the bound bone; smooth -> raw model space (identity).
                M = _mat_id() if smooth else self.bone_matrix.get(bt[0], _mat_id())
                isz=DT_SIZE[prm.index_type]; ifmt=DT_FMT[prm.index_type]
                ibase=self.idx_ptr + prm.first*isz
                idxs=struct.unpack_from("<%d%s"%(prm.count,ifmt), b, ibase)
                bibase,_=self.vatr["boneIndices"]; bisz=DT_SIZE[bi.data_type]; bifmt=DT_FMT[bi.data_type]
                verts=[]
                for idx in idxs:
                    pos=self.read_attr(sepd.attrs["position"],"position",idx,3)
                    nrm=self.read_attr(sepd.attrs["normal"],"normal",idx,3) if has_normal else (0,0,1)
                    uv =self.read_attr(sepd.attrs["texCoord0"],"texCoord0",idx,2)
                    Mv=M
                    if rigid_pv:
                        li=struct.unpack_from("<"+bifmt,b,bibase+bi.start+idx*bd*bisz)[0]
                        gb=bt[int(li)] if 0<=int(li)<len(bt) else bt[0]
                        Mv=self.bone_matrix.get(gb,_mat_id())
                    pos=_mat_apply_pos(Mv,pos); nrm=_mat_apply_dir(Mv,nrm)
                    verts.append((idx,pos,nrm,uv))
                for i in range(0,len(verts)-2,3):
                    yield (mesh.sepd_index, mesh.material_index, verts[i:i+3])


def to_obj(cmb:Cmb, path:str):
    vs=[]; vts=[]; vns=[]; faces=[]
    vmap={}
    for sepd_i,mat_i,tri in cmb.triangles():
        fi=[]
        for idx,pos,nrm,uv in tri:
            key=(pos,nrm,uv)
            if key not in vmap:
                vs.append(pos); vns.append(nrm); vts.append(uv)
                vmap[key]=len(vs)
            fi.append(vmap[key])
        faces.append(fi)
    with open(path,"w") as f:
        for p in vs:  f.write("v %.6f %.6f %.6f\n"%p)
        for t in vts: f.write("vt %.6f %.6f\n"%(t[0],t[1]))
        for n in vns: f.write("vn %.6f %.6f %.6f\n"%n)
        for fc in faces:
            f.write("f %d/%d/%d %d/%d/%d %d/%d/%d\n"%(
                fc[0],fc[0],fc[0],fc[1],fc[1],fc[1],fc[2],fc[2],fc[2]))
    return len(vs),len(faces)


if __name__=="__main__":
    import sys
    path=sys.argv[1] if len(sys.argv)>1 else "scratch/extract/tubo2_model.cmb"
    c=Cmb(open(path,"rb").read())
    print(f"CMB {c.name} v{c.version} bones={len(c.bones)} meshes={len(c.meshes)} "
          f"sepds={len(c.sepds)} textures={len(c.textures)} indices={c.index_count}")
    for t in c.textures:
        print(f"  tex {t.name:20} {t.width}x{t.height} fmt=0x{t.fmt:x} etc1={t.etc1} len={t.data_len}")
    for m in c.materials:
        print(f"  mat{m.index}: tex0={m.tex0_idx} min/mag=0x{m.min_filter:x}/0x{m.mag_filter:x} "
              f"wrapS=0x{m.wrap_s:x} wrapT=0x{m.wrap_t:x} "
              f"scale=({m.scale_s:.3f},{m.scale_t:.3f}) trans=({m.trans_s:.3f},{m.trans_t:.3f}) "
              f"rot={m.rot:.3f} cull={m.cull} alphaTest={m.alpha_test}@{m.alpha_ref:.2f}")
        if m.blend_enable:
            print(f"       BLEND src/dst rgb=0x{m.blend_src_rgb:x}/0x{m.blend_dst_rgb:x} "
                  f"a=0x{m.blend_src_a:x}/0x{m.blend_dst_a:x} eq=0x{m.blend_eq_rgb:x}/0x{m.blend_eq_a:x} "
                  f"const={tuple(round(v,2) for v in m.blend_color)} depthWrite={m.depth_write} "
                  f"{'(ADDITIVE)' if m.blend_dst_rgb==1 else ''}")
    for mesh in c.meshes:
        print(f"  mesh sepd={mesh.sepd_index} mat={mesh.material_index} -> tex{c.material_texture(mesh.material_index)}")
    for i,s in enumerate(c.sepds):
        pa=s.attrs["position"]
        print(f"  sepd{i} prims={s.prim_count} pos(dt=0x{pa.data_type:x} scale={pa.scale} mode={pa.mode} start={pa.start})")
    outobj=path.rsplit(".",1)[0]+".obj"
    nv,nf=to_obj(c,outobj)
    print(f"wrote {outobj}: {nv} verts, {nf} tris")
    # bbox sanity
    import struct as _s
    xs=[];ys=[];zs=[]
    for _,_,tri in c.triangles():
        for _,pos,_,_ in tri:
            xs.append(pos[0]);ys.append(pos[1]);zs.append(pos[2])
    if xs:
        print(f"  bbox x[{min(xs):.2f},{max(xs):.2f}] y[{min(ys):.2f},{max(ys):.2f}] z[{min(zs):.2f},{max(zs):.2f}]")
