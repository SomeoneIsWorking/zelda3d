#!/usr/bin/env python3
"""Oracle per-draw isolation: map every PICA draw of one frame to its screen footprint.

The vsuni log tells us WHAT lighting state each draw used; it cannot tell us WHICH
surface that draw painted. This tool closes that gap (the "draw -> material mapping"
that the Zora's Domain lighting divergence was blocked on):

  savestate  ->  for each draw n:  loadstate; drawskip n; run 1; snapshot
                 diff vs the unmodified frame  ->  exact pixels draw n contributes

Because every probe frame starts from the SAME save state, the diff is caused only by
the suppressed draw -- no animation drift.

Usage:
    python3 tools/oracle_draw_isolate.py <entrance-hex> <outdir> [--time 0x6000] [--settle 4]
Outputs in <outdir>:
    vsuni.log          per-draw uniform + identity log for the probe frame
    base.az.ppm        the unmodified frame
    draws.json         [{n, px, bbox, mean_before, mean_after, uni}]
    report.txt         human-readable table sorted by pixel count
"""
import json
import os
import shutil
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))
from harness_cache import OracleCache  # noqa: E402
from harness_gameplay import boot_to_gameplay, set_time_of_day  # noqa: E402
from harness_paths import TITLE_STATE  # noqa: E402
from harness_process import spawn  # noqa: E402


def read_ppm(path):
    with open(path, 'rb') as f:
        assert f.readline().strip() == b'P6'
        line = f.readline()
        while line.startswith(b'#'):
            line = f.readline()
        w, h = map(int, line.split())
        assert int(f.readline()) == 255
        return w, h, bytearray(f.read(w * h * 3))


def main():
    ent = int(sys.argv[1], 0)
    outdir = sys.argv[2]
    tod = 0x6000
    settle = 4
    for i, a in enumerate(sys.argv):
        if a == '--time':
            tod = int(sys.argv[i + 1], 0)
        if a == '--settle':
            settle = int(sys.argv[i + 1])
    os.makedirs(outdir, exist_ok=True)
    ap = os.path.abspath

    cache = OracleCache(TITLE_STATE)
    cache_args = {
        'probe': 'oracle-draw-isolate',
        'entrance': ent,
        'daytime': tod,
        'settle': settle,
        'warm': 2,
        'probe_frames': 4,
    }
    cached_outputs = (
        'camera.txt', 'texpack.txt', 'probe.state', 'vsuni.log', 'base.az.ppm',
        'draws.json', 'masks.npz', 'report.txt',
    )
    cached_paths = {
        filename: cache.get_artifact(
            f'oracle-draw-isolate:{filename}', cache_args
        )
        for filename in cached_outputs
    }
    if all(path is not None for path in cached_paths.values()):
        for filename, source in cached_paths.items():
            shutil.copyfile(source, os.path.join(outdir, filename))
        print('oracle: cache hit (key=%s)' % cache.key)
        return

    h = spawn(save_state=str(TITLE_STATE))
    print(boot_to_gameplay(h, entrance=ent))
    set_time_of_day(h, tod)
    for _ in range(settle):
        h.send('run 60')

    # Record the live camera basis WITH the state: every comparison against our
    # engine must be at a matched camera (unmatched framing produced a bogus 33%
    # figure on 2026-07-22).
    cam = h.send('az_camera')
    open(os.path.join(outdir, 'camera.txt'), 'w').write(cam + '\n')
    print(cam)

    # Record the HI-RES TEXTURE PACK state alongside the masks. This is not a
    # nicety: on 2026-07-22 an entire "renderer deficit" (Zora's ground 0.79 /
    # walls 0.86, the `render.zora-ground-deficit` frontier step) turned out to
    # be nothing but these oracle frames having been captured VANILLA while our
    # side rendered the 4K pack -- the pack's Zora rock/ground replacements are
    # ~20% darker than the ROM texels. tools/tev_mask_ratio.py HARD-FAILS when
    # this file disagrees with the pack state of the frame it is handed, so the
    # asymmetry can never again be read as a shader bug.
    tp = h.send('texpack')
    open(os.path.join(outdir, 'texpack.txt'), 'w').write(tp + '\n')
    print(tp)

    state = ap(os.path.join(outdir, 'probe.state'))
    print(h.send('savestate ' + state))

    # WARM-UP: a single retro_run() straight after loadstate renders a CORRUPT
    # frame (the Vulkan HW renderer's caches/framebuffers are not part of the
    # save state -- verified 2026-07-22: garbage tile blocks). From >=3 frames
    # the output is bit-exact reproducible across loadstates, so every probe
    # runs WARM frames, then ONE probed frame.
    warm = 2
    # PRESENT LAG: the captured framebuffer trails the emulated GPU by two
    # frames (verified 2026-07-22: drawskip has zero effect for nrun=1 and 2,
    # and bites at nrun=3). So hold the skip latch across PROBE frames and
    # snapshot after the last one.
    # OoT3D renders one 3D frame per TWO retro_run calls (30 fps logic on a
    # 60 Hz libretro cadence) -- verified by the per-frame vsuni line counts.
    # So a probe must span >=2 retro_runs to contain a drawn frame, and the
    # present lag adds two more.
    probe_frames = 4

    # 1) uniform + identity log for the probe frame
    log = ap(os.path.join(outdir, 'vsuni.log'))
    h.send('loadstate ' + state)
    h.send('run %d' % warm)
    h.send('vsuni_log ' + log)
    h.send('run 2')   # exactly one DRAWN frame
    h.send('vsuni_log off')
    uni = [l for l in open(log).read().splitlines() if l.startswith('draw ')]
    ndraw = len(uni)
    print('draws in probe frame:', ndraw)

    # 2) unmodified reference frame from the same state
    h.send('loadstate ' + state)
    h.send('drawskip off')
    h.send('run %d' % (warm + probe_frames))
    # snapshot replies with a 4-line block -> must drain it (send() reads one line)
    h.send_multiline('snapshot ' + ap(os.path.join(outdir, 'base')))
    w, hgt, base = read_ppm(os.path.join(outdir, 'base.az.ppm'))
    base_np = np.frombuffer(bytes(base), np.uint8).reshape(hgt, w, 3)
    print('frame', w, 'x', hgt)

    # 3) per-draw isolation
    rows = []
    masks = {}
    only = None
    if '--only' in sys.argv:
        only = {int(x) for x in sys.argv[sys.argv.index('--only') + 1].split(',')}
    probe = ap(os.path.join(outdir, 'probe'))
    for n in range(ndraw):
        if only is not None and n not in only:
            continue
        h.send('loadstate ' + state)
        h.send('run %d' % warm)
        h.send('drawskip %d' % n)
        h.send('run %d' % probe_frames)
        h.send_multiline('snapshot ' + probe)
        h.send('drawskip off')
        _, _, cur_b = read_ppm(os.path.join(outdir, 'probe.az.ppm'))
        cur = np.frombuffer(bytes(cur_b), np.uint8).reshape(hgt, w, 3)
        mask = (cur != base_np).any(axis=2)
        px = int(mask.sum())
        if px:
            ys, xs = np.nonzero(mask)
            bbox = [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())]
            mb = [round(float(v), 1) for v in base_np[mask].mean(axis=0)]
            ma = [round(float(v), 1) for v in cur[mask].mean(axis=0)]
        else:
            bbox = mb = ma = None
        row = {'n': n, 'px': px, 'bbox': bbox,
               'mean_before': mb, 'mean_after': ma,
               'uni': uni[n]}
        if px:
            # The mask IS the draw -> surface mapping: it lets the same surface
            # be measured on our side at a matched camera, instead of comparing
            # hand-drawn screen regions.
            masks['d%d' % n] = mask
        rows.append(row)
        print(n, px, row['bbox'], row['mean_before'])

    np.savez_compressed(os.path.join(outdir, 'masks.npz'), **masks)
    json.dump(rows, open(os.path.join(outdir, 'draws.json'), 'w'), indent=1)
    with open(os.path.join(outdir, 'report.txt'), 'w') as f:
        for r in sorted(rows, key=lambda r: -r['px']):
            f.write('n=%-3d px=%-7d bbox=%-24s rgb=%s\n  %s\n'
                    % (r['n'], r['px'], r['bbox'], r['mean_before'], r['uni']))
    for filename in cached_outputs:
        cache.put_artifact(
            f'oracle-draw-isolate:{filename}',
            cache_args,
            os.path.join(outdir, filename),
        )
    print('oracle: cached capture (key=%s)' % cache.key)
    print('wrote', outdir)
    h.close()


if __name__ == '__main__':
    main()
