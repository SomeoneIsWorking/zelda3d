// OoT3D cutscene opcodes — enumerated from FUN_002c5ba0 (the OoT3D CS
// interpreter) via oot3d-decomp/build/decomp/002c5ba0.c case blocks.
//
// See debug_journal/2026-07-04-cs-interpreter-located.md and
// debug_journal/2026-07-04-cs-format-is-n64-shape.md.
//
// Encoding (CORRECTED 2026-07-07): the stream starts at the " BDQ"
// header {magic, u16 ver, u16 0, s32 cmd_count, s32 end_frame}; most
// commands are (opcode u32, sub_count u32, sub_count * 48B records —
// the N64 CsCmdActorAction shape) but strides are OPCODE-SPECIFIC:
// cam cmds 1/2/5/6 = 12B hdr + 16B atoms, 7/8 = 28B, 0x8c = 12B recs,
// 0x97 = length-prefixed "ccb" camera-spline block, 1000 = 16B.
// Full walker: tools/walk_oot3d_cs.py; port: zelda3d_cutscene.cpp.
//
// The 0x0B..0x50 range mirrors N64 z_demo opcodes semantically; where
// the semantics match, the port reuses SoH's existing handlers. The
// 0x51..0x8E range is OoT3D-added; each needs case-by-case port from
// its case block in FUN_002c5ba0.

#ifndef ZELDA3D_CUTSCENE_OOT3D_OPCODES_H
#define ZELDA3D_CUTSCENE_OOT3D_OPCODES_H

// Full opcode set found in FUN_002c5ba0's switch. ~100 distinct.
// Names in the N64-mirror range (0x0B..0x50) match SoH's CS_CMD_*
// where the interpreter behavior matches; the "?" names are placeholder
// pending per-case decomp of FUN_002c5ba0.

// N64-mirror range: 0x0B..0x50 — most reuse SoH's z_demo handlers
#define OOT3D_CS_MISC_ACTION            0x0B  // -> SoH CS_CMD_MISC (func_80064824)
#define OOT3D_CS_SET_LIGHTING           0x0C  // -> SoH CS_CMD_SET_LIGHTING
#define OOT3D_CS_PLAY_BGM               0x0D
#define OOT3D_CS_STOP_BGM               0x0E
#define OOT3D_CS_FADE_BGM               0x0F
#define OOT3D_CS_TRANS_FX               0x10
#define OOT3D_CS_TERMINATOR             0x11
#define OOT3D_CS_TEXTBOX                0x12
#define OOT3D_CS_SCENE_TRANS_FX         0x13
#define OOT3D_CS_MOTIONBLUR             0x14
#define OOT3D_CS_GIVE_TATL              0x15
#define OOT3D_CS_TRANSITION             0x16
#define OOT3D_CS_TIME_ADVANCE           0x17
#define OOT3D_CS_START_SEQ              0x18
#define OOT3D_CS_STOP_SEQ               0x19
#define OOT3D_CS_TERRAIN_SHAKE          0x1A
#define OOT3D_CS_RUMBLE                 0x1B
#define OOT3D_CS_UNK_1C                 0x1C
#define OOT3D_CS_UNK_1D                 0x1D
#define OOT3D_CS_UNK_1E                 0x1E
#define OOT3D_CS_UNK_1F                 0x1F
#define OOT3D_CS_UNK_20                 0x20
#define OOT3D_CS_UNK_21                 0x21
#define OOT3D_CS_UNK_22                 0x22
#define OOT3D_CS_UNK_23                 0x23
#define OOT3D_CS_UNK_24                 0x24
#define OOT3D_CS_UNK_25                 0x25
#define OOT3D_CS_UNK_26                 0x26
#define OOT3D_CS_UNK_27                 0x27
#define OOT3D_CS_UNK_28                 0x28
#define OOT3D_CS_UNK_29                 0x29
#define OOT3D_CS_UNK_2A                 0x2A
#define OOT3D_CS_UNK_2B                 0x2B
#define OOT3D_CS_UNK_2C                 0x2C
#define OOT3D_CS_UNK_2D                 0x2D
#define OOT3D_CS_UNK_2E                 0x2E
#define OOT3D_CS_UNK_2F                 0x2F
#define OOT3D_CS_UNK_30                 0x30
#define OOT3D_CS_UNK_31                 0x31
#define OOT3D_CS_UNK_32                 0x32
#define OOT3D_CS_UNK_33                 0x33
#define OOT3D_CS_UNK_34                 0x34
#define OOT3D_CS_UNK_35                 0x35
#define OOT3D_CS_UNK_36                 0x36
#define OOT3D_CS_UNK_37                 0x37
// 0x38 absent from switch
#define OOT3D_CS_UNK_39                 0x39
#define OOT3D_CS_UNK_3A                 0x3A
// 0x3B absent from switch
#define OOT3D_CS_UNK_3C                 0x3C
// 0x3D absent
#define OOT3D_CS_UNK_3E                 0x3E
#define OOT3D_CS_UNK_3F                 0x3F
#define OOT3D_CS_UNK_40                 0x40
#define OOT3D_CS_UNK_41                 0x41
#define OOT3D_CS_UNK_42                 0x42
#define OOT3D_CS_UNK_43                 0x43
#define OOT3D_CS_UNK_44                 0x44
#define OOT3D_CS_UNK_45                 0x45
#define OOT3D_CS_UNK_46                 0x46
// 0x47 absent
#define OOT3D_CS_UNK_48                 0x48
// 0x49 absent
#define OOT3D_CS_UNK_4A                 0x4A
#define OOT3D_CS_ACTOR_CUE_4B           0x4B  // stores puVar20 into param_2+0x50 during window
#define OOT3D_CS_UNK_4C                 0x4C
#define OOT3D_CS_UNK_4D                 0x4D
#define OOT3D_CS_UNK_4E                 0x4E
#define OOT3D_CS_UNK_4F                 0x4F
#define OOT3D_CS_UNK_50                 0x50

// OoT3D-added range: 0x51..0x8E
#define OOT3D_CS_UNK_51                 0x51
#define OOT3D_CS_UNK_52                 0x52
#define OOT3D_CS_UNK_53                 0x53
#define OOT3D_CS_UNK_54                 0x54
#define OOT3D_CS_UNK_55                 0x55
#define OOT3D_CS_ACTOR_CUE_56           0x56  // fires FUN_00490ea4 on frame match
#define OOT3D_CS_ACTOR_CUE_57           0x57  // fires FUN_00490e50 on frame match
#define OOT3D_CS_UNK_58                 0x58
#define OOT3D_CS_UNK_59                 0x59
#define OOT3D_CS_UNK_5A                 0x5A
// 0x5B..0x5C absent
#define OOT3D_CS_UNK_5D                 0x5D
#define OOT3D_CS_UNK_5E                 0x5E
// 0x5F..0x68 absent
#define OOT3D_CS_UNK_69                 0x69
#define OOT3D_CS_UNK_6A                 0x6A
#define OOT3D_CS_UNK_6B                 0x6B
#define OOT3D_CS_UNK_6C                 0x6C
// 0x6D absent
#define OOT3D_CS_UNK_6E                 0x6E
#define OOT3D_CS_UNK_6F                 0x6F
// 0x70..0x71 absent
#define OOT3D_CS_UNK_72                 0x72
#define OOT3D_CS_UNK_73                 0x73
#define OOT3D_CS_UNK_74                 0x74
#define OOT3D_CS_UNK_75                 0x75
#define OOT3D_CS_UNK_76                 0x76
#define OOT3D_CS_UNK_77                 0x77
#define OOT3D_CS_UNK_78                 0x78
#define OOT3D_CS_UNK_79                 0x79
// 0x7A absent
#define OOT3D_CS_UNK_7B                 0x7B
#define OOT3D_CS_ACTOR_CUE_7C           0x7C  // fires FUN_003655d0 on frame in window
#define OOT3D_CS_UNK_7D                 0x7D
#define OOT3D_CS_UNK_7E                 0x7E
#define OOT3D_CS_UNK_7F                 0x7F
#define OOT3D_CS_UNK_80                 0x80
#define OOT3D_CS_UNK_81                 0x81
#define OOT3D_CS_UNK_82                 0x82
#define OOT3D_CS_UNK_83                 0x83
#define OOT3D_CS_UNK_84                 0x84
#define OOT3D_CS_UNK_85                 0x85
#define OOT3D_CS_UNK_86                 0x86
#define OOT3D_CS_UNK_87                 0x87
// 0x88 falls through to caseD_2c per the interpreter's compound switch
#define OOT3D_CS_UNK_88                 0x88
#define OOT3D_CS_UNK_89                 0x89
#define OOT3D_CS_UNK_8A                 0x8A
#define OOT3D_CS_UNK_8B                 0x8B
#define OOT3D_CS_UNK_8C                 0x8C  // reads Vec3 unsigned into float
#define OOT3D_CS_UNK_8D                 0x8D
#define OOT3D_CS_UNK_8E                 0x8E
#define OOT3D_CS_UNK_8F                 0x8F  // stores puVar20 into param_2+0x68 during window
// 0x90 falls through to caseD_f
#define OOT3D_CS_UNK_90                 0x90
#define OOT3D_CS_UNK_91                 0x91
#define OOT3D_CS_UNK_96                 0x96
#define OOT3D_CS_UNK_97                 0x97

// Terminator (also matches N64 CS_CMD_STOP).
#define OOT3D_CS_STOP                   0xFFFFFFFFu

// GREZZO CS container header — 32 bytes preceding the (cs_len,
// cmd_count, unk, unk, end_frame) cs-data header at file-offset +0x18.
//
// Layout (byte offsets into cs blob):
//   +0x00..0x0F: 16-byte hash / signature (per-cs)
//   +0x10..0x13: magic " BDQ" (LE 0x51444220)
//   +0x14..0x17: version = 3
//   +0x18..0x1B: header_size = 8
//   +0x1C..0x1F: cs command-stream length
//   +0x20..0x23: cmd_count (== N64's totalEntries)
//   +0x24..0x27: unk = 1
//   +0x28..0x2B: unk = 1 (setup0) / 0x38 (setup7)
//   +0x2C..0x2F: end_frame
//   +0x30..    : cmd stream (same shape as N64 z_demo)
#define OOT3D_CS_GREZZO_MAGIC           0x51444220u   // " BDQ" LE
#define OOT3D_CS_GREZZO_VER             3
#define OOT3D_CS_GREZZO_HDR_BYTES       0x30          // prefix bytes before cmds
#define OOT3D_CS_GREZZO_CMDCOUNT_OFFSET 0x20
#define OOT3D_CS_GREZZO_ENDFRAME_OFFSET 0x2C

#endif  // ZELDA3D_CUTSCENE_OOT3D_OPCODES_H
