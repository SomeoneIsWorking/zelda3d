---
id: C047
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

The auto-scale bbox measure is REPRODUCIBLE run-to-run, except for actors whose N64 draw changes shape

## Evidence

Same sweep run twice across 10 scenes: 19 of 20 objects produced BIT-IDENTICAL h/x/z ratios. The single exception is /actor/zelda_bombf.zar (En_Bombf, the bomb flower), whose ratios swung h=0.02186->0.06354->0.01084 and x=0.02739->0.94981->0.02739 across three runs -- a 34x drift on X. Its rendered scale was therefore luck of the frame under height-primary. Axis consensus refuses it in 2 of 3 runs and takes the coherent X/Z pair in the third.

## What would falsify it

a second actor showing >2% ratio drift between identical runs, which would mean the measure itself is noisy rather than this one actor being animated
