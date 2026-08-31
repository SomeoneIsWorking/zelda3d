#pragma once

// Diagnostic controls exported by the patched in-process Azahar renderer.
extern "C" {

extern char soh3d_draw_log_path[256];
extern int soh3d_draw_log_active;
extern char soh3d_vsuni_log_path[256];
extern int soh3d_vsuni_log_active;
extern char soh3d_lighting_capture_path[256];
extern int soh3d_lighting_capture_draw;
extern int soh3d_lighting_log_selftest_draw;

// The draw index resets every frame. Setting draw_skip suppresses one draw so
// a frame difference can localize its contribution.
extern int soh3d_draw_index;
extern int soh3d_draw_skip;

int soh3d_fog_dump(char* out, int cap);

} // extern "C"
