# 2026-07-02 — Zora's Domain (ent 0x108, scene 0x58) — inline fix log

Landed inline fix during this sweep: Obj_Syokudai wooden-torch variant now routes to
the correct `syokudai_ki_model.cmb`, extending sActorForcedAuto with a params-aware
match (paramMask + paramValue). Golden-torch default keeps AUTO's syokudai_gn pick.
Timed-torch (params >> 12 == 1) → syokudai_model.cmb mapping is less certain and
still falls through to AUTO for now; land when confirmed against a live oracle.

Close-test: `tools/syokudai_cmb_close_test.py` warps to ent 0x108 dayTime 0x8001,
asserts the syokudai_ki forced-CMB load line + the syokudai_gn AUTO default line.
TDD-verified: red-on-HEAD (stash + rebuild → wooden torch missing), green-with-fix.
EN_TG close-test still passes (params-aware helper handles the paramMask=0 case).

Sweep-discovered gaps that are NOT filed as cards (per project doctrine — sweep-
discovered parity is fixed in-session or noted here as the durable record):
- Player (Link) — separate track (#117 pipeline).
- Navi (EN_ELF) — universal actor, being ported now (#140).
- En_A_Obj, Obj_Mure, Obj_Mure2 — gameplay_keep small-prop family, will consolidate
  into a single inline port when Market Day is closed.
