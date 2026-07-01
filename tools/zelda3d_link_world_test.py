#!/usr/bin/env python3
"""Test the WORLD-ORIENTATION retarget offline against the OoT3D CSAB (best-available proxy).

The N64 jointTable and the OoT3D CSAB are INDEPENDENTLY authored ports of the same motion with
different rest frames, so the CSAB is not a clean function of the jointTable (even legs differ
~20deg). The faithful retarget transfers the N64 MOTION onto the OoT3D rig via world orientations,
not by reproducing the CSAB. This script computes, per bone, the local rotation the world-orientation
retarget would produce from the live N64 jointTable and compares its distance to the CSAB pose vs
the current "replace" rule. Lower (and especially much lower for the divergent spine/arm bones) =
world-orientation is the better, principled retarget.

Model (N64 bind = identity jointTable rotations; skin bound there):
  W_n64_rot[L](t)   = prod of ZYX(jointTable) along N64 chain to limb L
  W_oot_restRot[B]  = prod of ZYX(rest rot) along OoT3D chain to bone B   (from CMB)
  desiredWorld[B]   = W_n64_rot[mapL] @ W_oot_restRot[B]
  localRot[B](t)    = W_oot_animRot[parent(B)]^-1 @ desiredWorld[B]
"""
import os,sys,numpy as np
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import cmb as C, csab as A
from ctr_romfs import CtrRom
from zar import Zar

BIN=np.pi/32768
def Rx(a):c,s=np.cos(a),np.sin(a);return np.array([[1,0,0],[0,c,-s],[0,s,c]])
def Ry(a):c,s=np.cos(a),np.sin(a);return np.array([[c,0,s],[0,1,0],[-s,0,c]])
def Rz(a):c,s=np.cos(a),np.sin(a);return np.array([[c,-s,0],[s,c,0],[0,0,1]])
def zyx(r):return Rz(r[2])@Ry(r[1])@Rx(r[0])

# N64 player child skeleton tree (from tools/skeldata/n64/link_child.txt): limb -> (child,sibling).
N64=[ # index: (child,sibling)  255=DONE
 (1,255),(2,9),(3,255),(4,6),(5,255),(255,255),(7,255),(8,255),(255,255),(10,255),
 (11,12),(255,255),(255,13),(14,16),(15,255),(255,255),(17,19),(18,255),(255,255),(255,20),(255,255)]
def n64_parents():
    par={0:-1}
    # replicate the engine DFS (child deeper keeps parent=cur; sibling same level parent=par[cur])
    stack=[(0,-1)]
    # build by walking children/siblings explicitly
    par={}
    def walk(idx,parent):
        if idx==255 or idx in par: return
        par[idx]=parent
        ch,sib=N64[idx]
        if ch!=255: walk(ch,idx)
        if sib!=255: walk(sib,parent)
    par[0]=-1
    ch,sib=N64[0]
    if ch!=255: walk(ch,0)
    return par

BONEMAP={0:-1,1:-1,2:-1,3:6,4:7,5:8,6:3,7:4,8:5,9:9,10:10,11:11,12:-1,
         13:-1,14:13,15:14,16:15,17:-1,18:16,19:17,20:18,21:-1,22:-1,23:-1,24:-1}

def main():
    rom=CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    z=Zar(rom.read(rom.get("/actor/zelda_link_child_new.zar")))
    m=C.Cmb(z.read([f for f in z.files if f.name=="child/model/childlink_v2.cmb"][0]))
    bones={b.id:b for b in m.bones}
    par=n64_parents()
    # OoT3D rest world rotations (rotation part only).
    def oot_rest_world():
        out={}
        def w(bid):
            if bid in out: return out[bid]
            b=bones[bid]; L=zyx(b.rot)
            out[bid]=L if b.parent<0 else oot_rest_world.cache[b.parent]@L
            return out[bid]
        oot_rest_world.cache=out
        for bid in sorted(bones): w(bid)
        return out
    OOTrest=oot_rest_world()

    # load all captures (both anims)
    caps={}
    animmap={}  # n64base->csab
    import re
    for ln in open("Shipwright/soh/src/zelda3d/zelda3d_player_animmap.inc"):
        g=re.search(r'"([^"]+)"\s*,\s*"([^"]+)"',ln)
        if g: animmap[g.group(1)]=g.group(2)
    for ln in open(sys.argv[1] if len(sys.argv)>1 else "scratch/bin/link_joints.csv"):
        if ln.startswith('#') or ln.startswith('cap,'):continue
        p=ln.strip().split(',')
        cap=int(p[0]);caps.setdefault(cap,{'cf':float(p[1]),'al':float(p[2]),'anim':p[3],'limbs':{}})
        caps[cap]['limbs'][int(p[4])]=(int(p[5]),int(p[6]),int(p[7]))
    csab_cache={}
    def getcsab(n64):
        cb=animmap.get(n64)
        if cb not in csab_cache:
            f=[f for f in z.files if f.name.endswith('/'+cb+'.csab')]
            csab_cache[cb]=A.Csab(z.read(f[0])) if f else None
        return csab_cache[cb]

    def n64_world(d):
        # rotation-only FK over N64 tree from jointTable (dump limb li = jointTable index; limb L rot = li (L+1))
        W={}
        def w(L):
            if L in W: return W[L]
            j=d['limbs'].get(L+1,(0,0,0))
            R=zyx((j[0]*BIN,j[1]*BIN,j[2]*BIN))
            p=par.get(L,-1)
            W[L]=R if p<0 else w(p)@R
            return W[L]
        for L in par: w(L)
        return W

    # accumulate residuals
    rep={bid:[] for bid in BONEMAP if BONEMAP[bid]>=0}
    wld={bid:[] for bid in BONEMAP if BONEMAP[bid]>=0}
    for cap,d in caps.items():
        cs=getcsab(d['anim'])
        if cs is None:continue
        fr=A._anim_frame(cs,d['cf'])
        Wn=n64_world(d)
        # animated OoT3D world rotations under the WORLD retarget (parent-first)
        Waw={}
        for bid in sorted(bones):
            b=bones[bid]; limb=BONEMAP.get(bid,-1)
            if limb>=0 and limb in Wn:
                desired=Wn[limb]@OOTrest[bid]
                Wp=Waw[b.parent] if b.parent>=0 else np.eye(3)
                local=Wp.T@desired
            else:
                local=zyx(b.rot)
                Wp=Waw[b.parent] if b.parent>=0 else np.eye(3)
            Waw[bid]=Wp@local
            if limb>=0 and limb in Wn:
                node=cs.node_for_bone(bid);(_s,_r,_t)=A.bone_local_trs(bones[bid],node,fr)
                Ao=zyx(_r)
                j=d['limbs'][limb+1];Rn=zyx((j[0]*BIN,j[1]*BIN,j[2]*BIN))
                rep[bid].append(np.linalg.norm(Ao-Rn))
                wld[bid].append(np.linalg.norm(Ao-local))
    print("bone limb  | replace->CSAB  world->CSAB   (lower=closer to authored OoT3D pose)")
    for bid in sorted(rep):
        if not rep[bid]:continue
        print(f"{bid:4d} {BONEMAP[bid]:4d}  |   {np.mean(rep[bid]):6.3f}        {np.mean(wld[bid]):6.3f}")

if __name__=="__main__":main()
