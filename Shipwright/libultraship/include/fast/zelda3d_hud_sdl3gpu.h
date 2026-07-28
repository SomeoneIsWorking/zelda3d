// Zelda3D HUD — SDL3 GPU implementation (unified op model). See zelda3d_hud_sdl3gpu.cpp.
//
// The HUD state + draw collector is now the Fast::Zelda3DHudRenderer member subsystem of the SDL3 GPU
// backend (declared in fast/backends/zelda3d_sdl3gpu.h). The public C-ABI (Zelda3D_Hud_*) in
// zelda3d_hud_sdl3gpu.cpp forwards to it via Fast::g_activeSdl3GpuApi->Hud(). Internal to libultraship.
#pragma once
