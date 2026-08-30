---
id: C092
kind: claim
status: holds
created: 2026-08-30
tags: cmb,renderer,primary,alpha
depends: tools/cmb_primary_corpus_survey.py#scan_candidates
---

## Claim

The retail OoT3D corpus contains 24 vertex-lit/no-color mesh-material uses with non-opaque MatDiffuse alpha, and every one consumes PRIMARY alpha in its authored TEV chain.

## Evidence

tools/cmb_primary_corpus_survey.py over the user ROM: files=1997 unlit_candidates=154 lit_alpha_candidates=24 parse_failures=0; alpha distribution 76x1, 102x1, 127x18, 178x2, 204x2; bottled-Poe close-test confirms c8 alpha 76/255, HasColor false, vertexLighting true, and stage-0 PRIMARY alpha source.

## What would falsify it

A corrected CMB material, mesh-attribute, or TEV layout changes the candidate set; any candidate has color data, is unlit, has opaque c8 alpha, or does not consume PRIMARY alpha; or either-answer falsifiers fail.
