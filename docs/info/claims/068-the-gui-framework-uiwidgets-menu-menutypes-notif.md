---
id: C068
kind: claim
status: holds
created: 2026-08-06
tags: gui,shared,cvar,migration
depends: Shipwright/soh/soh/Notification/Notification.cpp, 2ship/2s2h/BenGui/Notification.cpp, Shipwright/soh/soh/cvar_prefixes.h
---

## Claim

> **NARROWED by C074 (2026-08-07).** The evidence below is about Notification.cpp and holds for it.
> The generalisation to "the GUI framework" does NOT: Menu.cpp's keys are already byte-identical
> across the games and UIWidgets hardcodes none. Read C074 before planning work around this.

The GUI framework (UIWidgets/Menu/MenuTypes/Notification) is NOT a mechanical de-duplication despite 76-90% similarity: the two games use DIFFERENT CVar keys for the same setting, so merging them silently changes persisted user config.

## Evidence

Notification.cpp reads CVarGetInteger(CVAR_SETTING("Notifications.Position"),3) in OoT and CVarGetInteger("gNotifications.Position",3) in MM. CVAR_SETTING(var) is CVAR_PREFIX_SETTING "." var (Shipwright/soh/soh/cvar_prefixes.h:7) and CVAR_PREFIX_SETTING is "gSettings" (Shipwright/CMake/soh-cvars.cmake:7), so the keys are gSettings.Notifications.Position vs gNotifications.Position. MM has no cvar_prefixes.h at all and uses raw literals throughout. Beyond the keys, MM's Notification is also functionally newer (size-scaled ItemSpacing/WindowPadding/icon, AlignTextToFramePadding) while OoT alone honours a Notifications.Mute CVar, and the two call different sound APIs (Audio_PlaySoundGeneral vs AudioSfx_PlaySfx).

## What would falsify it

if MM adopts the CVAR_* prefix macros with a config-version updater that migrates the old keys, the GUI framework becomes a genuine refactor rather than a user-visible migration
