---
id: C091
kind: claim
status: holds
created: 2026-08-30
tags: cmb,renderer,primary
depends: tools/cmb_primary_corpus_survey.py#candidates, Shipwright/cmb3d/asset/cmb.cpp#Cmb::buildDrawGroupsSkinned
---

## Claim

The retail OoT3D corpus contains 154 distinct unlit/no-color mesh-material uses where CmbVShader PRIMARY comes from a non-white authored MatDiffuse RGBA value.

## Evidence

tools/cmb_primary_corpus_survey.py over the user ROM: files=1997 candidates=154 parse_failures=0; exact branch is CmbVShader words 112-120 in oot3d-decomp/docs/title_env_lighting.md §10.2a; dungeon candle close-test confirms parser/group transport.

## What would falsify it

A corrected CMB material/attribute layout or retail-ROM scan produces a different candidate set, any reported candidate is vertex-lit or has color data, or the survey fails either-answer falsifiers.
