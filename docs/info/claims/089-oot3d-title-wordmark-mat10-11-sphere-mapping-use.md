---
id: C089
kind: claim
status: holds
created: 2026-08-30
tags:
depends: Shipwright/soh/src/zelda3d/behaviors/title/title_logo.cpp, Shipwright/libultraship/src/fast/backends/unified_shader.cpp
---

## Claim

OoT3D title wordmark mat10/11 sphere mapping uses CmbVShader c4-c6 identity, not the live title-camera basis

## Evidence

CmbVShader uniform table names c4-c7 uModelView; disassembly words 59-61 transform normal and 295-296 compute 0.5*n.xy+0.5. Cache-backed cs1093/az2010 vsuni capture records draws 75-87 c4-c6 identity, c10-c12 identity, c92 mapping method 3. Host selected TEX0 changes from (148,28,16) to the decoded linear center sample (209,42,58).

## What would falsify it

A cache-backed capture at another title cursor records non-identity c4-c6 for a wordmark draw, the title draw's CPU writer is proven to upload a camera-dependent matrix, or the CmbVShader normal path is corrected by byte-level disassembly.
