// SoH3D PC HUD — SDL3 GPU implementation (unified op model). See soh3d_hud_sdl3gpu.cpp.
//
// The HUD state + draw collector is now the Fast::SoH3DHudRenderer member subsystem of the SDL3 GPU
// backend (declared in fast/backends/soh3d_sdl3gpu.h). The public C-ABI (SoH3D_Hud_*) in
// soh3d_hud_sdl3gpu.cpp forwards to it via Fast::g_activeSdl3GpuApi->Hud(). Internal to libultraship.
#pragma once
