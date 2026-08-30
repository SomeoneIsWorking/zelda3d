---
id: C093
kind: claim
status: holds
created: 2026-08-30
tags: cmb,renderer,fragment-lighting
depends: tools/cmb_fragment_lighting_survey.py#scan_materials
---

## Claim

The retail OoT3D corpus has 205 fragment-light-enabled CMB materials: 197 consume FRAGMENT_PRIMARY, 69 consume FRAGMENT_SECONDARY, eight consume neither, and five additional materials consume a fragment source while the unit is disabled.

## Evidence

tools/cmb_fragment_lighting_survey.py over the user ROM: files=1997 materials=11172 fragment_enabled=205 enabled_primary_consumers=197 enabled_secondary_consumers=69 source_without_flag=5 flag_without_source=8 parse_failures=0; Dark Link retail close-test confirms disabled + active FRAGMENT_PRIMARY transport.

## What would falsify it

A corrected CMB material/TEV layout changes any count, an active source slot is misclassified, the retail close-test disagrees, or either-answer falsifiers fail.
