#include "repl_help.h"

#include <cstdio>

namespace HarnessRepl {

void PrintHelp() {
    std::fprintf(stderr, "soh3d_harness commands:\n"
                         "  run <N>              advance N frames\n"
                         "  r8|r16|r32 <va>      read u8/u16/u32 at VA (0x prefix ok)\n"
                         "  w8|w16|w32 <va> <v>  write u8/u16/u32 at VA\n"
                         "  mem <va> <n>         hex-dump N bytes (N<=4096)\n"
                         "  input <mask>         set held button mask (RETRO_DEVICE_JOYPAD)\n"
                         "                       B=0 Y=1 SELECT=2 START=3 UP=4 DOWN=5\n"
                         "                       LEFT=6 RIGHT=7 A=8 X=9 L=10 R=11\n"
                         "  loadstate <path>     load Azahar save state from file\n"
                         "  savestate <path>     write Azahar save state to file\n"
                         "  playstate            print PlayState pointer + mode=play|title\n"
                         "  gameplay             ok yes|no — TRUE only in a real gameplay scene;\n"
                         "                       gate warp/snapshot on this, not on playstate\n"
                         "  scene                print current sceneNum\n"
                         "  warp <entrance>      write nextEntranceIndex + trigger=20\n"
                         "  actors               dump current-scene actor table\n"
                         "  soh_boot             bring up SoH3D (InitOTR/Heaps_Alloc/Main_Init)\n"
                         "  soh_step <N>         advance SoH3D by N frames (RunFrame x N)\n"
                         "  step <N>             advance BOTH engines in lockstep (Azahar\n"
                         "                       retro_run + SoH3D RunFrame, per frame).\n"
                         "                       DEFAULT title-sync: first call auto-loads\n"
                         "                       title_settled.state + soh_boot and content-\n"
                         "                       locks the oracle to SoH's title cs from soh\n"
                         "                       frame 408 on (see title_sync.h). Skipped if\n"
                         "                       loadstate/soh_boot already ran manually.\n"
                         "  titlesync             print TitleSyncController state/counters\n"
                         "  compare <sub>        side-by-side dump from both engines;\n"
                         "                       `compare list` shows subs (scene/player/\n"
                         "                       actors/lighting)\n"
                         "  force <sub>          write state into BOTH engines (RE, no\n"
                         "                       inputs); `force list` shows subs\n"
                         "  snapshot <basepath>  write both fbs as <basepath>.{az,soh}.ppm\n"
                         "  drawskip <n>|off     suppress per-frame PICA draw #n (draw isolation)\n"
                         "  soh_drawlist         list native group/material ids on next host frame\n"
                         "  soh_drawskip <n>|off suppress native Zelda3D group #n\n"
                         "  sweep <sub>          automated multi-step parity driver;\n"
                         "                       `sweep list` shows subs (title, ...)\n"
                         "  texpack              hi-res texture-pack state + hit counters for\n"
                         "                       BOTH sides (ZELDA3D_HARNESS_TEXPACK=on|off)\n"
                         "  diag                 print harness diagnostics (input+capture)\n"
                         "  quit                 exit\n"
                         "  help                 this list\n");
    std::printf("ok\n");
}

} // namespace HarnessRepl
