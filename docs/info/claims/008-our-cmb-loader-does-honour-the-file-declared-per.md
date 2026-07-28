---
id: C008
kind: claim
status: holds
created: 2026-07-28
tags: 
---

## Claim

Our CMB loader DOES honour the file-declared per-attribute scale, including for vertex colour — the wrong-scale hypothesis is ruled out

## Evidence

Shipwright/cmb3d/asset/cmb.cpp: parseSepd() reads each attribute descriptor's start/scale/data_type/mode/constant (a.scale = f32(b, o+4), ~line 522-530), and readAttr() (line 583-593) is the SINGLE path used by position, normal, texcoord AND colour alike, ending in out[i] = dtRead(b, off + i*sz, attr.data_type) * attr.scale. dtRead decodes by the declared data_type (BYTE/UBYTE/SHORT/USHORT/INT/UINT/FLOAT) with no per-attribute assumption. There is no hardcoded /255 or /127 anywhere on the vertex-colour path — the hardcoded divisors elsewhere in that file are for material RGBA8 colours and light-direction vectors. Colour is read at line 889 via the same call. So the declared scale (the CMB's own counterpart of the 3DS attribute-scale register, e.g. 1/255) is applied by construction, and cannot be the source of a brightness divergence.

## What would falsify it

A CMB whose colour attribute uses mode==1 (CONSTANT), since readAttr returns attr.constant[] WITHOUT applying scale — that is believed correct because PICA fixed attributes bypass the scale register, but it is an ASSUMPTION and has not been verified against the hardware/decomp. If the mesh in question turns out to use a constant colour attribute, re-open this.
