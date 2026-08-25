// title_sync.cpp — see title_sync.h for the architecture writeup. This TU
// only holds the controller instance + the settled-state path; the actual
// arm sequence (loadstate + soh_boot, with title_settle.py auto-gen on a
// missing file) lives in title_sync_runtime.cpp since it needs
// Core::System / soh_runtime.cpp's SoH bring-up symbols that this tiny TU
// deliberately doesn't pull in.
#include "title_sync.h"

TitleSyncController g_titleSync;

const char* const kTitleSettledStatePath = "scratch/title_settled.state";
