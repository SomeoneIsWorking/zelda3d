---
id: C048
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

An auto-routed prop's scale is actor->scale whenever the measurement CONFIRMS the CMB is 1:1 on any axis

## Evidence

Zelda3D_EmitModelDraw applies worldScale uniformly and never multiplies actor->scale, so the scale had to be inferred from pixels. But when the CMB is dimensionally 1:1 with the N64 display list the ratio IS actor->scale -- exact, per-axis, noise-free. The measurement's job is therefore to CONFIRM the 1:1, not to produce the number. Audited across all 13 routed scenes with every room swept (scratch/routing_audit.txt): 35 of 36 forced slots resolved, 28 confirmed 1:1, and only 7 move by more than 2%. The three cases the change was built for are verified live: m_Hgiro guillotine 0.03004->0.10000 (+232.9%, a row previously WITHDRAWN as unshippable, now submits=17289), m_Hkhuta coffin lid 0.08463->0.10000 (+18.2%), m_Hkenzan 0.09379->0.10000 (+6.6%, shipped silently small the pass before). Critically it does NOT fire wrongly where axis-agreement did: Obj_Syokudai's re-authored torch confirms on height alone and lands at 1.00000 (+0.6% from the old value, not -26.6%), and d_lift at 0.10000 (-1.3%, not +116%).

## What would falsify it

a prop whose Draw applies an extra Matrix_Scale AND whose measured ratio still lands within 2% of actor->scale on some axis by coincidence -- that would make the confirmation a false positive. Two routings currently rest on a SINGLE confirming axis with a >2% move (zelda_bwall -14.3% on [x], zelda_bombiwa +4.3% on [z]) and are the most likely place for that to show up.
