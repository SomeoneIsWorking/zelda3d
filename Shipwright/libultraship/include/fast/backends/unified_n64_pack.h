#pragma once

#include "fast/unified_material.h"

struct CCFeatures; // interpreter.h

// Render-unification effort (kanban #131), Phase 3 groundwork. Converts the N64 color-combiner
// decode (CCFeatures, interpreter.h) into UnifiedMaterial. This is the EASY half of Phase 3 —
// combMux is a direct copy since both use the same SHADER_* operand codes (see
// unified_material.h's header comment). The N64 unified draw route consumes this packer.
//
// NOT included here (the genuinely hard, not-yet-done half of Phase 3): repacking N64's per-
// triangle vertex data into UnifiedVtx. Unlike CCFeatures -> UnifiedMaterial, that requires
// replicating interpreter.cpp's tri-emit resolution of PRIM/ENV/LOD-fraction combiner register
// values from live RDP state (Interpreter::GfxSpTri1/2, ~interpreter.cpp:2178-2385) — those values
// don't live in the simple per-vertex LoadedVertex struct. PackN64VertexToUnified resolves those
// values from the live emitted vertex stream before this material is submitted.
UnifiedMaterial Fast_PackCCFeaturesToUnifiedMaterial(const CCFeatures& cc);
