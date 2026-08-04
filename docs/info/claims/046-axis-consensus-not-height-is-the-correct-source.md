---
id: C046
kind: claim
status: falsified
created: 2026-08-04
tags: 
falsified_on: 2026-08-04
---

## Claim

Axis CONSENSUS, not height, is the correct source for an auto-routed prop's scale

## Evidence

Height, X and Z are three independent estimates of the same scale. Measured across 21 derives in 10 scenes (scratch/1to1_data.txt): where two axes agree within 2% and the third dissents, the dissenter is a bad measure every time. Verified on both cases the change was designed for: Bottom of the Well coffin lid m_Hkhuta h=0.08463 vs x=z=0.09999 (+18.1%), Shadow Temple guillotine m_Hgiro h=0.03004 vs x=z=0.09999 (+232.9%, the row that had been WITHDRAWN as unshippable). A shipped routing was also silently 6.6% small: m_Hkenzan h=0.09379 vs x=z=0.09999. Regression-checked: of 21 derives, 10 take no consensus at all and 9 of the 11 that do move by <=0.6%.

## What would falsify it

a prop where two axes agree within 2% and the AGREEING pair is demonstrably the wrong scale (i.e. a re-authored mesh that happens to be re-authored on exactly two axes by the same factor)

## FALSIFIED 2026-08-04

FALSIFIED THE SAME DAY IT WAS FILED, by the full 13-scene routing audit it motivated. The claim's own recorded falsifier was 'a re-authored mesh re-authored on exactly two axes by the same factor' -- and that is not a rare edge case, it is EVERY PROP WITH A SQUARE OR ROUND FOOTPRINT. X and Z are not independent measurements for such a prop; they are one measurement taken twice, so they agree automatically even when the mesh is genuinely re-authored chunkier in plan. Axis consensus therefore moved two props it must not have: Obj_Syokudai's torch (h=0.99363 x=0.73296 z=0.72523, actor->scale 1.0) by -26.6%, and zelda_d_lift (h=0.10129 x=0.21038 z=0.22751, actor->scale 0.1) by +116.2% -- the latter a row an earlier pass had investigated and deliberately left alone (007a0fc7). It also moved zelda_hidan_objects by +500% and zelda_jya_iron by +98.8% on no evidence. SUPERSEDED BY C048: anchor on actor->scale, not on axes agreeing with each other.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
