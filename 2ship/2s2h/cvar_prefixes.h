/**
 * Majora's Mask's CVar namespaces.
 *
 * The MM counterpart of Ocarina of Time's soh/cvar_prefixes.h. It exists because the two games'
 * namespaces are DIFFERENT and must stay that way until a config migration says otherwise: OoT
 * writes cosmetics under "gCosmetics", MM under "gCosmetic", and silently unifying them would
 * rewrite users' persisted settings (claim C068).
 *
 * Until this file existed, MM had no such home. `CVAR_PREFIX_AUDIO` and `CVAR_PREFIX_COSMETIC` were
 * #defined inline in the two .cpp/.h files that happened to need them -- and a third file,
 * AudioCollection.cpp, used `CVAR_PREFIX_AUDIO` while defining only `CVAR_AUDIO`, so it silently
 * resolved against OoT's global -DCVAR_PREFIX_AUDIO. That worked only because OoT's prefixes were
 * add_compile_definition'd at the ROOT of the build and reached every target, MM included. Now that
 * they are scoped to OoT's own target where they belong, MM has to say what its namespaces are.
 * See claim C072.
 *
 * Values here must match what MM already PERSISTS -- they name keys in 2ship2harkinian.json, so
 * changing one is a user-visible migration, not a rename.
 *
 * NOT here: the engine's own namespaces (CVAR_PREFIX_CONTROLLERS, CVAR_PREFIX_ADVANCED_RESOLUTION,
 * and the "gSettings." / "gOpenWindows." keys built in Shipwright/CMake/lus-cvars.cmake). Those are
 * shared by both games by design and come from libultraship's build.
 */

#ifndef MM_CVAR_PREFIXES_H
#define MM_CVAR_PREFIXES_H

#define CVAR_PREFIX_AUDIO "gAudioEditor"
#define CVAR_AUDIO(var) CVAR_PREFIX_AUDIO "." var

// Singular, unlike OoT's "gCosmetics". MM also has six raw "gCosmetics.*" literals (the tunic
// colours in BenPort.cpp and RainbowSync/RainbowSpeed/RandomizeOnSeedGen in CosmeticEditor.cpp), so
// its cosmetics are currently split across two namespaces. That is a real defect, but fixing it
// moves persisted keys, so it belongs in a ConfigVersion2Updater rather than here.
#define CVAR_PREFIX_COSMETIC "gCosmetic"
#define CVAR_COSMETIC(var) CVAR_PREFIX_COSMETIC "." var

#endif // MM_CVAR_PREFIXES_H
