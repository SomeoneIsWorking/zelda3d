// GENERATED: base table by tools/zelda3d_link_retarget_derive.py (idle-fit: head/spine/legs),
// then the 6 ARM rows (b14/15/16 L, b18/19/20 R) OVERLAID from tools/zelda3d_carry_corr_derive.py.
//
// ORACLE-VERIFIED BONE MAP: ../../../../oot3d-decomp/docs/link_bone_map.md (read this).
// The full N64-limb<->OoT3D-bone correspondence was verified against the LIVE OoT3D game (Azahar
// oracle, link_skel_live.py) + static CMB mesh->bone bindings. OoT3D inserts an extra chest bone
// (b10) between the upper-body root (b9=UPPER) and the head/arms; eyes+mouth bind to b11 (=HEAD),
// cap to b12 (=HAT). So the correct drive is b9<-UPPER, b10<-REST (extra), b11<-HEAD, b12<-rest
// (the cap rigidly tracks b11; its N64-HAT replace residual 0.868 means the rest frames diverge,
// so replace would discard the cap orientation — rest is faithful).
//
// SPINE-SHIFT FIX APPLIED 2026-06-23: the table previously drove b10<-HEAD and b11<-HAT (HEAD/HAT
// one bone too low), so N64 HEAD pitch tilted the CHEST instead of the head -> #8 head-down /
// #83 dialog arms. Idle hid it (HEAD rot~=0). Now b10=rest, b11<-HEAD(n64Limb 10). The b9 UPPER
// correction is unchanged (its per-bone idle-fit is independent of the b10/b11 mapping; re-deriving
// reproduces the same -8.8deg left-C). b11<-HEAD pure-replace residual is 0.306, on par with the
// known-good legs (0.33-0.57) — verified by tools/zelda3d_link_retarget_derive.py with the corrected
// BONEMAP. Gated on tools/zelda3d_anim_qa.py before/after.
//
// #8 ROOT-CAUSE FIX (2026-06-21): the previous table's head bone b10 carried a hand-woven
// correction that TWISTED the whole upper body ~126° and pitched the head/chest forward (b10 is
// the OoT3D CHEST — head AND arms hang off it), so 3dn Link "looked at the ground" with arms
// crossed in front. b10 (and b9 spine, b11 neck) are now the QUANTITATIVE idle-fit vs the OoT3D
// own idle CSAB (nml_wait_free), residual ~0.1: head level, torso untwisted, arms hang at sides.
// VERIFIED live: plain idle (front/side) + walk cycle (legs cycle, arms swing, no stiffening).
//
// The ARM rows are mode-4 (C·R·C2) fitted to TWO pose constraints — plain idle (OoT3D
// nml_wait_free, IDLE_FROM_CSAB=1) AND carry (OoT3D nml_carryB_wait) — so ONE correction serves
// both #8 idle (arms down) and #6/#85 carry (arms up). The previous carry-fit pinned its idle
// constraint to the item-B idle / old output, leaving PLAIN idle unconstrained -> the crossed-arm
// regression. A ~0.8-rad residual is irreducible: Grezzo's OoT3D arm motion differs in magnitude
// from N64's (a constant two-sided correction preserves the idle->carry angle, which differs), so
// the fit is the best single-correction compromise across both poses.
// Per OoT3D childlink_v2 bone: { n64Limb, mode, C[9], C2[9] }. mode 0=rest, 1=replace
// (local rot := N64 jointTable rot), 2=left (C·R_n64), 3=right (R_n64·C), 4=two-sided
// (C·R_n64·C2). C/C2 are row-major 3x3 constant rest-frame corrections; identity unless
// used. Legs/neck stay on pure replace (verified, no regression); divergent spine/arms
// get a correction. Derived QUANTITATIVELY from the live N64 jointTable (idle+run) vs the
// OoT3D CSAB (best proxy for the authored pose); a residual floor is irreducible
// authoring noise (the N64 anim and OoT3D CSAB are independently authored).
static const Zelda3dBoneCorr kLinkChildBoneCorr[25] = {
    {  -1, 0, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f }, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f } }, // b0
    {  -1, 0, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f }, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f } }, // b1
    {  -1, 0, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f }, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f } }, // b2
    {   6, 1, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f }, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f } }, // b3
    {   7, 1, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f }, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f } }, // b4
    {   8, 1, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f }, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f } }, // b5
    {   3, 1, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f }, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f } }, // b6
    {   4, 1, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f }, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f } }, // b7
    {   5, 1, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f }, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f } }, // b8
    {   9, 2, { 0.999926f,-0.001493f,-0.012109f,0.003337f,0.988111f,0.153705f,0.011736f,-0.153734f,0.988043f }, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f } }, // b9
    {  -1, 0, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f }, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f } }, // b10 chest = REST (extra OoT3D bone, no N64 limb; oracle-verified spine fix 2026-06-23)
    {  10, 1, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f }, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f } }, // b11 head <- N64 HEAD (limb 10); replace residual 0.306 ~= legs
    {  -1, 0, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f }, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f } }, // b12
    {  -1, 0, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f }, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f } }, // b13
    {  13, 4, { -0.021007f,-0.937732f,0.346723f,0.204548f,-0.343495f,-0.916609f,0.978631f,0.051666f,0.199027f }, { 0.437235f,-0.113691f,-0.892132f,-0.002496f,0.991820f,-0.127619f,0.899344f,0.058026f,0.433375f } }, // b14 CARRY-FIT (idle+carryB_wait)
    {  14, 4, { -0.814186f,-0.079300f,0.575163f,-0.023676f,0.994339f,0.103579f,-0.580121f,0.070715f,-0.811455f }, { 0.158752f,-0.870577f,-0.465718f,-0.870577f,0.099072f,-0.481955f,0.465718f,0.481955f,-0.742176f } }, // b15 CARRY-FIT (idle+carryB_wait)
    {  15, 4, { 0.928754f,-0.094366f,0.358484f,0.159691f,0.974573f,-0.157183f,-0.334536f,0.203231f,0.920208f }, { 0.983768f,0.158778f,0.083611f,-0.134524f,0.960912f,-0.241973f,-0.118763f,0.226797f,0.966674f } }, // b16 CARRY-FIT (idle+carryB_wait)
    {  -1, 0, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f }, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f } }, // b17
    {  16, 4, { 0.600692f,-0.181695f,-0.778560f,0.788871f,-0.023416f,0.614113f,-0.129812f,-0.983076f,0.129268f }, { 0.821310f,-0.158183f,0.548114f,0.121203f,0.987238f,0.103298f,-0.557459f,-0.018407f,0.830000f } }, // b18 CARRY-FIT (idle+carryB_wait)
    {  17, 4, { -0.965282f,-0.165856f,-0.201798f,-0.142511f,0.981834f,-0.125272f,0.218910f,-0.092164f,-0.971383f }, { 0.062303f,-0.973475f,0.220148f,-0.973475f,-0.010618f,0.228547f,-0.220148f,-0.228547f,-0.948315f } }, // b19 CARRY-FIT (idle+carryB_wait)
    {  18, 4, { 0.881043f,-0.318199f,0.350018f,0.289428f,0.947888f,0.133189f,-0.374159f,-0.016040f,0.927226f }, { 0.997004f,0.068752f,0.035450f,-0.064421f,0.991678f,-0.111464f,-0.042819f,0.108847f,0.993136f } }, // b20 CARRY-FIT (idle+carryB_wait)
    {  -1, 0, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f }, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f } }, // b21
    {  -1, 0, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f }, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f } }, // b22
    {  -1, 0, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f }, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f } }, // b23
    {  -1, 0, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f }, { 1.000000f,0.000000f,0.000000f,0.000000f,1.000000f,0.000000f,0.000000f,0.000000f,1.000000f } }, // b24
};
