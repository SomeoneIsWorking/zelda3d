---
id: I042
kind: instrument
status: trusted
created: 2026-08-28
---

## Instrument

Native Zelda3D sgdrawlist/sgdrawskip group discriminator

## Validated by

2026-08-28 forced BossFd2 body listed one exact seven-group material signature and skipping listed draw 37 changed 29,009 host RGB channels while the unadvanced Oracle repeat changed 0. Trusted for group/material identity and skip delivery only; attribution requires byte-identical restored host repeat bases.

The restored fixed-position capture then produced byte-identical bases in both engines:

```text
oracle 92b19296713b05b4701c8b37d5ea31c31bc140787ec390fe17dec8da6b04d33b
host   133a70872b81a3bd3ef79dfe83108be3956625f2b8f3e6507f974dc496528f4f
```

Expanded vertex counts map the native body list to the oracle PICA draws as follows:

```text
host 37/0/1 -> oracle 29,30    (537 + 165 = 702)
host 38/1/2 -> oracle 31       (198)
host 39/2/0 -> oracle 32,33,34 (417 + 417 + 240 = 1074)
host 40/3/3 -> oracle 35       (120)
host 41/4/4 -> oracle 36       (498)
host 42/5/5 -> oracle 37       (258)
host 43/6/5 -> oracle 38       (102)
```

Oracle skip masks were nonempty for 29, 30, 31, 32, 33, 35, 36, and 37; 34 and 38 were fully
occluded in the presented frame. Host skips 37 through 42 were nonempty; host 43 was fully
occluded. This proves the mapping and control delivery, but not a material cause: all visible groups
participate in the composite, and the engines use different global screen placement and texture-pack
state. The next step is a matched per-material output comparison at the corrected camera.

## Known failure modes

- Per-frame draw indices are engine-local and can shift when actor/effect submissions change. Map
  them independently through each engine's draw list and require the same model-local group/material
  signature after every checkpoint restore.
- Sequential base/skip frames can differ because animation, particles, camera, or lighting advanced.
  They validate skip delivery only. Material attribution requires two byte-identical unmodified
  repeats from the same restored checkpoint before comparing either engine's skipped frame.

## Related renderer-boundary finding

The same restored BossFd2 run compared native `sgdump` material chains with the oracle's PICA
`vsuni_log`. It found and corrected a generic packing error: PICA alpha source selectors occupy
bits 16/20/24 of the source word, while the host had used 12/16/20. Material 1 now emits
`0e300430/0e1f0e43/0e1f0edf/0e1f0eef`, matching oracle `e300430/e1f0e43/e1f0edf/e1f0eef` after
leading-zero normalization. This is an alpha-path correctness fix, not evidence that the remaining
opaque body brightness has the same cause; the material residual stays open.

The embedded harness exposes `soh_sgdump <modelId>` to repeat this material-boundary observation.
