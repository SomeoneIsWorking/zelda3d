---
id: 22
title: Clang structure gate blocked by overgrown SoH enhancement monoliths
status: resolved
symptom: committed SoH enhancement growth exceeded frozen legacy line ceilings before format and tidy could run
tags: tooling,structure,blocker
created: 2026-08-30
updated: 2026-09-05
---

The old files mixed command groups, editor panels, randomizer rules and option registration,
speech tables, actor actions, and C save adapters. Those responsibilities now live in their
own modules listed in `docs/codemap.md`; remaining legacy ceilings were ratcheted downward,
not increased. Settings registration stays on the long-lived `Settings` owner so stored
callbacks cannot retain a destroyed stack-local registrar.

The normal Clang verifier passes structure and formatting across the changed sources and
lint against the actual Clang compilation database. Clean hosted checkouts also compare
committed changes and ceiling history against the event's base commit; synthetic two-commit
tests prove that committed ceiling increases and legacy-source growth are rejected.
