#!/usr/bin/env python3
"""title_daytime_verify.py — at each content-matched (az,soh) title pair,
read BOTH engines' live gSaveContext.dayTime, SoH's envCtx skybox/ambient
(soh_env), and sample the rendered sky RGB from the snapshot. This is the
proof that SoH's title clock now tracks the oracle's (dusk where the
oracle is dusk, not day-blue).

Uses the same forward-only two-engine stepping as title_ab.py: run Az to
`az`, soh_step SoH to `soh`. Reads:
  - az_daytime         -> Az gSaveContext.dayTime  (fixed .bss VA)
  - soh_env            -> SoH dayTime, skybox1/2, blend, lightCtx ambient
  - snapshot sky RGB   -> mean of an upper-sky region on each engine

Usage:
    source .env
    tools/title_daytime_verify.py 200:397 360:449 550:593
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
from harness_process import spawn  # noqa: E402
from harness_paths import TITLE_STATE  # noqa: E402
import title_ab  # noqa: E402

OUTDIR = REPO / "scratch" / "title_daytime"
SAVESTATE = TITLE_STATE


def sky_rgb(png_path, box=(0, 0, 270, 90)):
    a = np.asarray(Image.open(png_path).convert("RGB"), dtype=np.float64)
    h, w, _ = a.shape
    x0, y0, x1, y1 = box
    x1 = min(x1, w); y1 = min(y1, h)
    reg = a[y0:y1, x0:x1].reshape(-1, 3)
    return tuple(reg.mean(axis=0))


def parse_pair(s):
    az, soh = s.split(":")
    return int(az), int(soh)


def main():
    pairs = [parse_pair(x) for x in sys.argv[1:]] or [(200, 397), (360, 449), (550, 593)]
    OUTDIR.mkdir(parents=True, exist_ok=True)
    for az, soh in pairs:
        # Fresh harness per pair (forward-only stepping).
        import os
        os.environ.setdefault("HARNESS_STDERR", str(REPO / "scratch" / "logs" / "title_dt_verify.log"))
        (REPO / "scratch" / "logs").mkdir(parents=True, exist_ok=True)
        h = spawn(save_state=str(SAVESTATE))
        r = h.send("soh_boot")
        if not r.startswith("ok"):
            print(f"soh_boot failed: {r}", file=sys.stderr); h.quit(); continue
        title_ab._step_chunked(h, "run", az)
        title_ab._step_chunked(h, "soh_step", soh)

        az_dt = h.send("az_daytime")
        soh_env = h.send("soh_env")
        base = OUTDIR / f"pair_{az}_{soh}"
        h.send_multiline(f"snapshot {base}")
        az_png = title_ab.ppm_to_png(str(base) + ".az.ppm")
        soh_png = title_ab.ppm_to_png(str(base) + ".soh.ppm")
        az_sky = sky_rgb(az_png)
        soh_sky = sky_rgb(soh_png)

        m = re.search(r"daytime=0x([0-9a-fA-F]+)", az_dt)
        azv = int(m.group(1), 16) if m else -1
        m2 = re.search(r"daytime=0x([0-9a-fA-F]+)", soh_env)
        sohv = int(m2.group(1), 16) if m2 else -1
        diff = (sohv - azv) if (azv >= 0 and sohv >= 0) else None

        print(f"=== pair az={az} soh={soh} ===")
        print(f"  az_daytime : 0x{azv:04x} ({azv})")
        print(f"  soh_env    : {soh_env}")
        print(f"  SoH dayTime: 0x{sohv:04x} ({sohv})   delta(soh-az)={diff}")
        print(f"  sky RGB    : az=({az_sky[0]:.0f},{az_sky[1]:.0f},{az_sky[2]:.0f}) "
              f"soh=({soh_sky[0]:.0f},{soh_sky[1]:.0f},{soh_sky[2]:.0f})")
        print(f"  snapshots  : {az_png}  {soh_png}")
        h.quit()


if __name__ == "__main__":
    main()
