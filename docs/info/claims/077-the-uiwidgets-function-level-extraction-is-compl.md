---
id: C077
kind: claim
status: holds
created: 2026-08-07
tags: n3,gui,shared,method
depends: Shipwright/soh/soh/SohGui/UIWidgets.hpp, 2ship/2s2h/BenGui/UIWidgets.hpp
---

## Claim

The UIWidgets function-level extraction is COMPLETE at 33 functions. Every remaining identical-bodied widget is blocked by rule 3, not by ShipInit: all EIGHT widget option structs (CheckboxOptions, InputOptions, Float/IntSliderOptions, ButtonOptions, RadioButtonsOptions, WidgetOptions, ComboboxOptions) differ between the games, and every remaining candidate takes one. Reconciling those structs is the prerequisite, and it is a design decision, not a merge.

## Evidence

Struct-by-struct compare of the two UIWidgets.hpp: CheckboxOptions ~8 of 42 lines differ, InputOptions ~7 of 73, FloatSliderOptions ~55 of 83/88, IntSliderOptions ~79 of 74/80, ButtonOptions ~7 of 29/25, RadioButtonsOptions ~3 of 25, WidgetOptions ~24 of 20/26, ComboboxOptions ~45 of 38/49 -- none identical. The five CVar* widgets pass rules 1 and 2 (bodies AND declarations identical) but take const CheckboxOptions&/InputOptions&/FloatSliderOptions&/IntSliderOptions&; RadioButton and StateButton take RadioButtonsOptions/ButtonOptions. CORRECTS the previous session note that named ShipInit::Init as the blocker for the five CVar widgets: it is not. Separately measured and still true, but a different task: the two games' ShipInit.hpp are CODE-IDENTICAL (differing only in include order and a doc comment) and are a genuine duplicate -- with 383 includers between them.

## What would falsify it

if the option structs are reconciled, every remaining identical-bodied widget becomes extractable and this ceases to be the limit
