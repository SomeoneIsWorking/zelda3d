#!/usr/bin/env python3
"""model_match.py — identify WHICH OoT3D CMB corresponds to an N64 display list, quantitatively.

The manual loop this replaces: spawn the object, force each candidate CMB through a bring-up
`gscale` slot, screenshot, eyeball it. That does not scale to hundreds of objects and it violates
the project rule "verify quantitatively — never eyeball renders". This tool makes identification a
batch, measured operation that emits a RANKED match list, a markdown/JSON report and ONE annotated
contact sheet.

Two subcommands:

    model_match.py capture <spec...>      drive the LIVE game to produce the PNGs
    model_match.py score <baseline> <empty|-> <label=cand.png>... [--sheet F] [--report F]

`score` is PURE: it reads PNGs off disk and never imports/touches the game. `capture` is the only
half that talks to the REPL, and it is never invoked by `score`.

    # capture (needs a running instance: tools/zelda3d_game.py start)
    tools/model_match.py capture --actor 0x12A --params 0 --entrance 0x00CD \\
        --scale 0.06 --cmb 1-11 --out scratch/model_match/switch

    # score (offline, works on any pre-existing shots)
    tools/model_match.py score scratch/screenshots/n64floor_0x00.png - \\
        scratch/screenshots/sw_*.png --sheet scratch/model_match/sheet.png

Everything is repo-relative; outputs default under scratch/model_match/.

--------------------------------------------------------------------------------------------
SCORING METHOD
--------------------------------------------------------------------------------------------
1. SUBJECT ISOLATION.  With an empty plate (same view, subject parked off-camera) the subject is
   simply the pixels differing from the plate.  WITHOUT a plate the background is estimated
   per-shot and locally: inside a centred ROI, each row's background is the median of that row's
   LEFT+RIGHT margins — the subject is centre-framed by capture construction (`acam`), so the
   margins are background by definition, and a per-row estimate follows the scene's vertical
   gradient (grass/lighting).  This also survives shots captured in DIFFERENT sessions, where a
   global plate would be invalid (different cloud/lighting state).
   Candidate blobs are then ranked by  area * exp(-(dist_to_frame_centre / sigma)^2)  — again
   using the centre-framing guarantee — and blobs spanning most of the ROI are rejected as failed
   isolation (horizon bands, whole-frame diffs), the local twin of the FULLFRAME_FRAC guard.
2. SHAPE  — IoU of the two masks cropped to their bboxes and resampled to NORM x NORM, so
   silhouette is compared independent of on-screen size (N64 and 3DS models differ in scale).
3. ASPECT — bbox w/h ratio compared BEFORE that normalisation (the size info shape throws away).
4. COLOUR — subject pixels only, and deliberately BRIGHTNESS-INVARIANT, because an N64 display
   list and its OoT3D CMB are lit/exposed differently (measured: N64 floor switches sit at
   val~0.2, the 3DS ones at 0.5-0.8, so any value-sensitive term measures the renderer rather
   than the object).  What survives is hue+saturation: a saturation-weighted, circularly smoothed
   hue histogram intersection + a chromaticity (brightness-normalised RGB) distance + saturation
   agreement.  This is what separates the gold/red/blue floor switches, whose silhouettes are
   identical.

    score = W_SHAPE*shape + W_COLOR*colour + W_ASPECT*aspect     (weights below, sum 1.0)

An unusable shot scores 0.0 and is still returned with a `reason`, so a candidate is never
silently dropped from the ranking.
"""

from __future__ import annotations

import argparse
import colorsys
import glob
import json
import math
import os
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from collections import deque
from dataclasses import dataclass, field
from typing import Optional, Tuple

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPL = os.path.join(REPO, "tools", "zelda3d_repl.py")
SHOTDIR = os.path.join(REPO, "scratch", "screenshots")
OUTDIR = os.path.join(REPO, "scratch", "model_match")


def _abspath(p):
    """Accept repo-relative or absolute paths (tools store repo-relative)."""
    if not p:
        return None
    return p if os.path.isabs(p) else os.path.join(REPO, p)


# =============================================================================================
# CAPTURE — drives the live REPL.  Importable; never used by `score`.
# =============================================================================================

class CaptureError(RuntimeError):
    """The REPL refused a command, or the game is not in the expected state."""


class CaptureTimeout(CaptureError):
    """A REPL round-trip or frame dump did not complete in time (never hang: always raise)."""


@dataclass(frozen=True)
class SpawnSpec:
    """Everything needed to put ONE subject in front of a fixed camera, reproducibly.

    entrance    entrance index to warp to before spawning (None = use the loaded scene).
    actor_id    actor id (or model-table name string) to spawn.
    params      init params (variant-gated actors need these, e.g. Obj_Switch type/subType).
    nth         which live instance to select (`asel <id> [n]`, 0 = nearest).
    cam_dist    `acam <dist> <axis>` framing distance.  Small props need a much closer camera
                than the 110 default or they occupy a handful of pixels and everything "matches".
    cam_axis    0 = look down +X, 1 = look down +Z (side profile).
    scale       world scale forced through `scale_slot` (0 = leave the behavior's default).
    scale_slot  gscale slot the behavior reads for world scale (Obj_Switch: 24).
    ident_slot  gscale slot the behavior reads to FORCE a CMB index (Obj_Switch: 25).  Required
                for capture_candidate; None means the subject has no forced-CMB knob.  There is
                no generic engine-wide CMB override — only behaviors that expose such a slot.
    freeze      afreeze mode: 1 = pin pos+rot (what makes framing repeatable), 2 = pos only, 0 off.
    settle      seconds after a state change before dumping.  LOAD-BEARING: headless frames
                advance slowly and a too-short settle returns the PREVIOUS frame, i.e. the
                previous candidate's CMB — a silent, ranking-destroying off-by-one.
    warp_settle seconds after a warp for the scene/room actors to finish spawning.
    away        world position the subject is parked at for capture_empty (out of frame).
    """
    actor_id: object
    entrance: Optional[int] = None
    params: int = 0
    nth: int = 0
    cam_dist: float = 110.0
    cam_axis: int = 0
    scale: float = 0.0
    scale_slot: Optional[int] = 24
    ident_slot: Optional[int] = 25
    freeze: int = 1
    settle: float = 0.8
    warp_settle: float = 4.0
    away: Tuple[float, float, float] = (0.0, -20000.0, 0.0)
    key: str = field(default="")

    def ident(self) -> str:
        """Stable identity of the STAGING (everything except which CMB is forced)."""
        return (f"{self.key or self.actor_id}|e={self.entrance}|p={self.params}|n={self.nth}"
                f"|cam={self.cam_dist},{self.cam_axis}|s={self.scale}@{self.scale_slot}"
                f"|f={self.freeze}")


def _run(args, timeout: float) -> str:
    try:
        r = subprocess.run(["python3", REPL] + list(args), cwd=REPO,
                           capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired as e:
        raise CaptureTimeout(f"zelda3d_repl.py {' '.join(map(str, args))}: "
                             f"no response in {timeout}s (game hung or dead?)") from e
    out = (r.stdout or "") + (r.stderr or "")
    if r.returncode != 0:
        raise CaptureError(f"zelda3d_repl.py {' '.join(map(str, args))} exited "
                           f"{r.returncode}: {out.strip()}")
    return out.strip()


def repl(cmd: str, timeout: float = 20.0) -> str:
    """Send one REPL command; return its reply. Raises on timeout — never hangs."""
    reply = _run(["cmd", cmd], timeout)
    if "(no reply)" in reply:
        # The C side did not acknowledge, so the caller's assumption about game state is
        # unverified. Surface it rather than capturing a frame of unknown state.
        raise CaptureError(f"REPL command {cmd!r} got no reply (instance wedged or command "
                           f"rejected before Zelda3D_ReplReply)")
    return reply


def _shot(out_png: str, timeout: float = 45.0) -> str:
    """Dump the current frame and move the PNG to `out_png`."""
    dst = _abspath(out_png)
    os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
    # Unique staging name so a stale scratch/screenshots/<name>.png can never be mistaken for a
    # fresh capture (a silent wrong-frame bug when a dump fails).
    name = f"mm_{os.getpid()}_{int(time.time() * 1000) % 1000000}"
    src = os.path.join(SHOTDIR, name + ".png")
    if os.path.exists(src):
        os.remove(src)
    _run(["shot", name], timeout)
    if not os.path.exists(src):
        raise CaptureError(f"shot {name}: no PNG produced at {src}")
    shutil.move(src, dst)
    ppm = os.path.join(SHOTDIR, name + ".ppm")
    if os.path.exists(ppm):
        os.remove(ppm)
    return dst


def _actor_tok(actor_id) -> str:
    """`spawn`/`asel` take either a model-table NAME or a numeric id; keep hex readable."""
    if isinstance(actor_id, str):
        return actor_id
    return f"0x{int(actor_id):X}"


def _parse_triplet(text: str, key: str) -> Optional[Tuple[float, float, float]]:
    """Pull `key(a,b,c)` (e.g. 'eye=(120,-30,44)') out of a REPL reply."""
    i = text.find(key)
    if i < 0:
        return None
    j = text.find("(", i)
    k = text.find(")", j)
    if j < 0 or k < 0:
        return None
    parts = text[j + 1:k].split(",")
    if len(parts) != 3:
        return None
    try:
        return tuple(float(p) for p in parts)  # type: ignore[return-value]
    except ValueError:
        return None


class Session:
    """Holds the staged subject + the LOCKED camera for one identification run.

    Framing invariance is the whole point: baseline, every candidate and the empty plate must be
    comparable outside the subject's silhouette.  So staging happens ONCE per SpawnSpec (re-staging
    per shot would drift the framing and invalidate every diff) and the camera is placed with
    `acam`, then RE-ASSERTED from its reported eye/at through the explicit `cam` command — which is
    independent of the actor, so parking the subject off-screen for the empty plate cannot drag the
    view with it.
    """

    def __init__(self):
        self.spec: Optional[SpawnSpec] = None
        self.staged_ident: Optional[str] = None
        self.cam: Optional[Tuple[float, ...]] = None
        self.pos: Optional[Tuple[float, float, float]] = None
        self.forced: Optional[int] = None
        self.parked = False

    def stage(self, spec: SpawnSpec, force: bool = False) -> None:
        """Warp / spawn / select / freeze / frame. No-op when `spec` is already staged."""
        if not force and self.staged_ident == spec.ident():
            return
        if spec.entrance is not None:
            repl(f"warp {spec.entrance}", timeout=60.0)
            time.sleep(spec.warp_settle)
        reply = repl(f"spawn {_actor_tok(spec.actor_id)} {spec.params}")
        if "FAILED" in reply:
            raise CaptureError(f"spawn failed for {spec.actor_id!r} params={spec.params}: {reply}")
        time.sleep(spec.settle)

        sel = repl(f"asel {_actor_tok(spec.actor_id)} {spec.nth}")
        if "no match" in sel:
            raise CaptureError(f"asel {spec.actor_id!r} #{spec.nth}: {sel}")
        self.pos = _parse_triplet(sel, "pos=")

        if spec.freeze:
            repl(f"afreeze {spec.freeze}")
        self.parked = False

        if spec.scale and spec.scale_slot is not None:
            repl(f"gscale {spec.scale_slot} {spec.scale}")

        acam = repl(f"acam {spec.cam_dist} {spec.cam_axis}")
        at = _parse_triplet(acam, "at=")
        eye = _parse_triplet(acam, "eye=")
        if at is None or eye is None:
            raise CaptureError(f"acam gave no eye/at to lock onto: {acam}")
        repl("cam %.3f %.3f %.3f %.3f %.3f %.3f" % (eye + at))
        self.cam = eye + at
        self.spec = spec
        self.staged_ident = spec.ident()
        self.forced = None
        time.sleep(spec.settle)
        # A warp draws a transient SCENE TITLE CARD ("Hyrule Field") over the frame for several
        # seconds. It is NOT cancelled by the empty plate (the plate is taken later, once it is gone),
        # so a baseline shot taken too early isolates the TITLE TEXT instead of the subject and every
        # score is garbage. Fixed sleeps are guesswork; wait for the frame to actually stop changing.
        self._wait_stable()

    def _wait_stable(self, interval: float = 1.0, tries: int = 12, thresh: float = 1.2) -> bool:
        """Block until two consecutive frames are ~identical (transient overlays finished).

        Returns True once stable, False if it never settled within `tries` (caller continues anyway —
        a noisy scene should not hard-fail a capture, but the caller is warned)."""
        try:
            from PIL import Image, ImageChops, ImageStat
        except Exception:
            time.sleep(interval * 3)  # no Pillow: fall back to a conservative wait
            return False
        prev = None
        with tempfile.TemporaryDirectory() as td:
            for i in range(tries):
                p = _shot(os.path.join(td, f"stab{i}.png"))
                cur = Image.open(p).convert("RGB")
                if prev is not None:
                    d = ImageStat.Stat(ImageChops.difference(prev, cur)).mean
                    if sum(d) / len(d) < thresh:
                        return True
                prev = cur
                time.sleep(interval)
        sys.stderr.write("model_match: frame never stabilised; captures may include a transient overlay\n")
        return False

    def _require_staged(self) -> SpawnSpec:
        if self.spec is None:
            raise CaptureError("nothing staged — call stage(spec)/capture_baseline(spec, ...) first")
        return self.spec

    def _park(self, away: bool) -> None:
        """Move the (frozen) subject far out of frame, or back to its staged position.

        There is no REPL 'despawn', and kill+respawn would re-randomise pose/position — exactly the
        framing drift the empty plate exists to control for.  `apos` moves the freeze pin too, so
        the subject stays parked.  If a behavior refuses to draw at an extreme Y, pass an in-bounds
        but off-camera `away`.
        """
        spec = self._require_staged()
        if away == self.parked:
            return
        target = spec.away if away else self.pos
        if target is None:
            raise CaptureError("no staged position recorded — cannot park/restore the subject")
        repl("apos %.3f %.3f %.3f" % tuple(target))
        self.parked = away
        time.sleep(spec.settle)

    def _force(self, cmb_n: Optional[int]) -> None:
        """Force OoT3D CMB index `cmb_n` through the behavior's ident slot (None/0 = clear)."""
        spec = self._require_staged()
        want = int(cmb_n) if cmb_n else 0
        if self.forced == want:
            return
        if spec.ident_slot is None:
            if want:
                raise CaptureError(f"spec has no ident_slot — cannot force CMB {want}")
            self.forced = 0
            return
        repl(f"gscale {spec.ident_slot} {want}")
        self.forced = want
        time.sleep(spec.settle)

    def capture_baseline(self, spec: SpawnSpec, out_png: str) -> str:
        self.stage(spec)
        self._park(False)
        self._force(0)  # no forced CMB -> the behavior's own path / the N64 display list
        return _shot(out_png)

    def capture_candidate(self, spec: SpawnSpec, cmb_n: int, out_png: str) -> str:
        self.stage(spec)
        self._park(False)
        self._force(cmb_n)
        return _shot(out_png)

    def capture_empty(self, out_png: str, spec: Optional[SpawnSpec] = None) -> str:
        if spec is not None:
            self.stage(spec)
        self._require_staged()
        self._park(True)
        return _shot(out_png)

    def release(self) -> None:
        """Undo every knob this session set.

        The game outlives the run and another agent may drive it next; leaving `cam` overridden or
        an ident slot forced would silently corrupt their observations.  `camfreeze 0` is the
        camera-override release (`gcam` is unrelated — it forces the camera behind Link).
        """
        if self.spec is None:
            return
        spec = self.spec
        try:
            self._park(False)
            if spec.ident_slot is not None:
                repl(f"gscale {spec.ident_slot} 0")
            if spec.scale and spec.scale_slot is not None:
                repl(f"gscale {spec.scale_slot} 0")
            if spec.freeze:
                repl("afreeze 0")
            repl("camfreeze 0")
        except CaptureError:
            pass
        finally:
            self.spec = None
            self.staged_ident = None
            self.cam = None
            self.forced = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.release()
        return False


_default: Optional[Session] = None


def session() -> Session:
    global _default
    if _default is None:
        _default = Session()
    return _default


def capture_baseline(spawn_spec: SpawnSpec, out_png: str) -> str:
    """Capture the subject rendering its ORIGINAL N64 model. Returns the written PNG path."""
    return session().capture_baseline(spawn_spec, out_png)


def capture_candidate(spawn_spec: SpawnSpec, cmb_n: int, out_png: str) -> str:
    """Capture the subject with OoT3D CMB `cmb_n` forced. Returns the written PNG path."""
    return session().capture_candidate(spawn_spec, cmb_n, out_png)


def capture_empty(out_png: str, spawn_spec: Optional[SpawnSpec] = None) -> str:
    """Capture the SAME frame with the subject parked off-screen (background plate)."""
    return session().capture_empty(out_png, spawn_spec)


def release() -> None:
    """Restore the game knobs the default session touched."""
    if _default is not None:
        _default.release()


# =============================================================================================
# SCORE — pure functions over PNG files.  Never touches the game.
# =============================================================================================

DIFF_THRESH = 30        # per-channel abs diff (0..255) for "this pixel is subject"
MIN_BLOB_PX = 40        # smaller blobs are noise
NORM = 64               # normalised mask resolution for the shape IoU
ASPECT_TOL = 0.80       # |log(ratio)| at which the aspect subscore hits 0 (~2.2x off)
HUE_BINS = 24
BORDER_FRAC = 0.04      # border ring width for the whole-frame background fallback
BLANK_PLATE_VAR = 4.0   # empty plate with per-channel spread below this counts as blank
FULLFRAME_FRAC = 0.85   # blob covering more of the frame than this = failed isolation
MAX_SAMPLES = 20000     # cap on subject pixels sampled for colour stats

# Centre-framing priors (see module docstring). The capture side puts the subject at frame centre
# via `acam`, so these are protocol guarantees, not fudge factors.
ROI = (0.30, 0.30, 0.70, 0.78)   # x0,y0,x1,y1 as fractions — search window for a plate-less shot
ROI_MARGIN = 0.22       # fraction of ROI width on each side used for the per-row background
CENTER_SIGMA = 0.06     # gaussian sigma (fraction of min(w,h)) on blob distance to frame centre
MAX_SPAN = 0.60         # blob spanning more than this of the ROI = background band, not a subject

W_SHAPE, W_COLOR, W_ASPECT = 0.45, 0.40, 0.15


def _load(path):
    if not path or not os.path.exists(_abspath(path) or ""):
        return None
    try:
        return Image.open(_abspath(path)).convert("RGB")
    except Exception:
        return None


def _spread(im):
    """Rough per-channel value spread (max-min of the extrema) — blank-plate detector."""
    return max(hi - lo for lo, hi in im.getextrema())


def _border_color(im):
    """Median colour of the frame's border ring — crudest background estimate."""
    w, h = im.size
    bw = max(1, int(min(w, h) * BORDER_FRAC))
    px = im.load()
    rs, gs, bs = [], [], []
    step = max(1, (2 * (w + h)) // 4000)
    for y in list(range(0, bw)) + list(range(h - bw, h)):
        for x in range(0, w, step):
            r, g, b = px[x, y]
            rs.append(r); gs.append(g); bs.append(b)
    for x in list(range(0, bw)) + list(range(w - bw, w)):
        for y in range(bw, h - bw, step):
            r, g, b = px[x, y]
            rs.append(r); gs.append(g); bs.append(b)
    if not rs:
        return (0, 0, 0)
    med = lambda v: sorted(v)[len(v) // 2]
    return (med(rs), med(gs), med(bs))


def _row_margin_bg(roi, margin=ROI_MARGIN):
    """Per-row background image for a centre-framed ROI.

    Each row's background colour is the MEDIAN of that row's left+right margins.  The subject is
    centred, so the margins contain no subject pixels; per-row (rather than one flat colour) tracks
    the scene's vertical gradient — grass brightness, horizon, lighting — which is what made a
    single flat/global background estimate light up half the frame.
    """
    rw, rh = roi.size
    px = roi.load()
    bg = Image.new("RGB", roi.size)
    bp = bg.load()
    mw = max(2, int(rw * margin))
    xs = list(range(0, mw)) + list(range(rw - mw, rw))
    for y in range(rh):
        row = [px[x, y] for x in xs]
        m = tuple(int(statistics.median(p[c] for p in row)) for c in range(3))
        for x in range(rw):
            bp[x, y] = m
    return bg


def _diff_mask(im, plate, thresh=DIFF_THRESH):
    """Binary 'L' mask of pixels differing from the plate, despeckled (erode then dilate)."""
    d = ImageChops.difference(im, plate)
    r, g, b = d.split()
    m = ImageChops.lighter(ImageChops.lighter(r, g), b)
    m = m.point(lambda v: 255 if v > thresh else 0)
    return m.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.MaxFilter(3))


def _blobs(mask, min_px=MIN_BLOB_PX):
    """All 4-connected components >= min_px, as [(pixel_indices, bbox), ...]."""
    w, h = mask.size
    data = mask.tobytes()  # 'L', 1 byte/px
    seen = bytearray(w * h)
    out = []
    for start in range(w * h):
        if data[start] == 0 or seen[start]:
            continue
        q = deque([start])
        seen[start] = 1
        px = [start]
        x0 = x1 = start % w
        y0 = y1 = start // w
        while q:
            i = q.popleft()
            x, y = i % w, i // w
            if x < x0: x0 = x
            if x > x1: x1 = x
            if y < y0: y0 = y
            if y > y1: y1 = y
            for j, ok in ((i - 1, x > 0), (i + 1, x < w - 1),
                          (i - w, y > 0), (i + w, y < h - 1)):
                if ok and data[j] and not seen[j]:
                    seen[j] = 1
                    q.append(j)
                    px.append(j)
        if len(px) >= min_px:
            out.append((px, (x0, y0, x1 + 1, y1 + 1)))
    return out


def _pick_subject(cands, frame_size, roi_size, offset=(0, 0), max_span=MAX_SPAN):
    """Choose the subject blob: largest, weighted by how centred it is in the FRAME.

    Both terms come from the capture protocol: `acam` frames the subject at the centre, and a
    centre-framed subject never spans the search window (a blob that does is a horizon band, a
    lighting change, or a failed isolation — the local twin of the FULLFRAME_FRAC guard).
    """
    fw, fh = frame_size
    rw, rh = roi_size
    cx, cy = fw / 2.0, fh / 2.0
    sigma = CENTER_SIGMA * min(fw, fh)
    best, best_w = None, 0.0
    for px, bb in cands:
        if (bb[2] - bb[0]) > max_span * rw or (bb[3] - bb[1]) > max_span * rh:
            continue
        bx = (bb[0] + bb[2]) / 2.0 + offset[0]
        by = (bb[1] + bb[3]) / 2.0 + offset[1]
        d = math.hypot(bx - cx, by - cy)
        wgt = len(px) * math.exp(-(d / sigma) ** 2)
        if wgt > best_w:
            best_w, best = wgt, (px, bb)
    return best


def isolate(im, plate):
    """-> dict(mask, bbox, area) for the subject in `im`, or None.

    With `plate` (a real empty-plate capture) the whole frame is diffed against it.  Without one,
    the search is confined to the centred ROI and the background is estimated per-row from the
    ROI's margins (see `_row_margin_bg`).
    """
    if im is None:
        return None
    w, h = im.size
    if plate is not None and plate.size != im.size:
        plate = plate.resize(im.size, Image.NEAREST)

    if plate is not None:
        mask = _diff_mask(im, plate)
        offset = (0, 0)
        region = (w, h)
    else:
        box = (int(ROI[0] * w), int(ROI[1] * h), int(ROI[2] * w), int(ROI[3] * h))
        roi = im.crop(box)
        mask = _diff_mask(roi, _row_margin_bg(roi))
        offset = (box[0], box[1])
        region = roi.size

    picked = _pick_subject(_blobs(mask), (w, h), region, offset)
    if picked is None:
        return None
    px, bb = picked
    if len(px) > FULLFRAME_FRAC * w * h:
        return None  # whole frame differs -> the plate does not describe this shot
    mw = region[0]
    full = bytearray(w * h)
    for i in px:
        full[(i // mw + offset[1]) * w + (i % mw + offset[0])] = 255
    bbox = (bb[0] + offset[0], bb[1] + offset[1], bb[2] + offset[0], bb[3] + offset[1])
    return {"mask": Image.frombytes("L", (w, h), bytes(full)), "bbox": bbox, "area": len(px)}


def _norm_mask(sub):
    """Crop the blob mask to its bbox and resample to NORM x NORM (re-binarised)."""
    c = sub["mask"].crop(sub["bbox"]).resize((NORM, NORM), Image.BILINEAR)
    return c.point(lambda v: 255 if v >= 128 else 0).tobytes()


def _shape_iou(a, b):
    inter = union = 0
    for i in range(NORM * NORM):
        pa = a[i] != 0
        pb = b[i] != 0
        if pa or pb:
            union += 1
            if pa and pb:
                inter += 1
    return (inter / union) if union else 0.0


def _aspect(bbox):
    x0, y0, x1, y1 = bbox
    return max(1, x1 - x0) / max(1, y1 - y0)


def _aspect_score(ar_b, ar_c):
    if ar_b <= 0 or ar_c <= 0:
        return 0.0
    return max(0.0, 1.0 - abs(math.log(ar_c / ar_b)) / ASPECT_TOL)


def _color_stats(im, sub):
    """Hue histogram (saturation-weighted) + mean RGB/S/V over subject pixels only.

    The hue histogram is weighted by SATURATION ALONE, not sat*val: an N64 display list and its
    OoT3D CMB are lit and exposed differently (measured on the floor switches: the N64 models sit
    at val~0.2, the 3DS ones at val~0.5-0.8), so a value-weighted histogram declares a plainly
    red/blue dark model "achromatic" and throws away the one cue that identifies it.
    """
    x0, y0, x1, y1 = sub["bbox"]
    px = im.load()
    mk = sub["mask"].load()
    n_bbox = max(1, (x1 - x0) * (y1 - y0))
    step = max(1, int(math.sqrt(n_bbox / MAX_SAMPLES)) if n_bbox > MAX_SAMPLES else 1)
    hist = [0.0] * HUE_BINS
    sr = sg = sb = ss = sv = 0.0
    n = 0
    for y in range(y0, y1, step):
        for x in range(x0, x1, step):
            if not mk[x, y]:
                continue
            r, g, b = px[x, y]
            hh, _l, _s = colorsys.rgb_to_hls(r / 255.0, g / 255.0, b / 255.0)
            v = max(r, g, b) / 255.0
            sat = 0.0 if v == 0 else (v - min(r, g, b) / 255.0) / v
            hist[min(HUE_BINS - 1, int(hh * HUE_BINS))] += sat
            sr += r; sg += g; sb += b
            ss += sat; sv += v
            n += 1
    if n == 0:
        return None
    # Smooth circularly before normalising: hue is a continuous, wrapping quantity and hard
    # bin edges would score two near-identical hues (a teal N64 texture vs its slightly bluer
    # 3DS CMB) as sharing nothing at all.
    hist = [0.25 * hist[(i - 1) % HUE_BINS] + 0.5 * hist[i] + 0.25 * hist[(i + 1) % HUE_BINS]
            for i in range(HUE_BINS)]
    tot = sum(hist)
    hist = [x / tot for x in hist] if tot > 1e-6 else [1.0 / HUE_BINS] * HUE_BINS
    return {"hist": hist, "mean": (sr / n, sg / n, sb / n),
            "sat": ss / n, "val": sv / n, "n": n, "chroma": tot / n}


def _color_score(a, b):
    """0..1. Hue-histogram intersection + CHROMATICITY distance + saturation agreement.

    Deliberately brightness-invariant: the same object rendered from an N64 display list and from
    an OoT3D CMB differs a lot in absolute brightness (different lighting model), so raw RGB
    distance and value agreement mostly measure the renderer, not the object's identity.  What
    survives the exposure difference is HUE and SATURATION, so the comparison happens on the
    chromaticity plane (r,g,b normalised by their sum) plus saturation.
    """
    if a is None or b is None:
        return 0.0
    inter = sum(min(x, y) for x, y in zip(a["hist"], b["hist"]))

    def chrom(m):
        s = sum(m) or 1.0
        return (m[0] / s, m[1] / s, m[2] / s)

    ca, cb = chrom(a["mean"]), chrom(b["mean"])
    d = math.sqrt(sum((x - y) ** 2 for x, y in zip(ca, cb)))
    chrom_s = max(0.0, 1.0 - d / 0.60)          # 0.60 ~ opposite-primary separation
    sat_s = max(0.0, 1.0 - abs(a["sat"] - b["sat"]))
    # Hue only carries information where there IS saturation; weight it by the LESS saturated of
    # the two subjects so two grey models fall back to chromaticity/saturation agreement.
    chroma = min(a["chroma"], b["chroma"])
    w_hue = 0.60 * max(0.0, min(1.0, chroma / 0.35))
    return w_hue * inter + (1.0 - w_hue) * (0.70 * chrom_s + 0.30 * sat_s)


def _plate_for(empty_png, ref_im):
    plate = _load(empty_png)
    if plate is not None and (plate.size != ref_im.size or _spread(plate) < BLANK_PLATE_VAR):
        return None  # mismatched or blank -> per-shot local background estimate instead
    return plate


def score_candidates(baseline_png, empty_png, candidates):
    """Rank OoT3D CMB candidate renders against an N64 baseline render.

    baseline_png : the N64 subject shot
    empty_png    : same view with no subject (None/missing/blank -> local background estimate)
    candidates   : {label: path}

    Returns dicts sorted best-first: {label, score, shape, color, aspect, bbox, area, path,
    reason}.  `reason` is "" for a normal result, otherwise why the candidate scored 0.
    """
    base_im = _load(baseline_png)
    if base_im is None:
        raise FileNotFoundError(f"baseline not readable: {baseline_png}")
    plate = _plate_for(empty_png, base_im)

    base_sub = isolate(base_im, plate)
    if base_sub is None:
        return [{"label": lbl, "score": 0.0, "shape": 0.0, "color": 0.0, "aspect": 0.0,
                 "bbox": None, "area": 0, "path": p, "png": p, "reason": "baseline has no subject"}
                for lbl, p in sorted(candidates.items())]

    base_norm = _norm_mask(base_sub)
    base_ar = _aspect(base_sub["bbox"])
    base_col = _color_stats(base_im, base_sub)

    out = []
    for label, path in candidates.items():
        rec = {"label": label, "score": 0.0, "shape": 0.0, "color": 0.0, "aspect": 0.0,
               "bbox": None, "area": 0, "path": path, "png": path, "reason": ""}
        im = _load(path)
        if im is None:
            rec["reason"] = "unreadable/missing image"
            out.append(rec)
            continue
        if im.size != base_im.size:
            im = im.resize(base_im.size, Image.BILINEAR)
        sub = isolate(im, plate)
        if sub is None:
            rec["reason"] = "no subject isolated (all background, blank frame, or whole-frame diff)"
            out.append(rec)
            continue
        shape = _shape_iou(base_norm, _norm_mask(sub))
        aspect = _aspect_score(base_ar, _aspect(sub["bbox"]))
        color = _color_score(base_col, _color_stats(im, sub))
        rec.update(bbox=tuple(sub["bbox"]), area=sub["area"],
                   shape=round(shape, 4), color=round(color, 4), aspect=round(aspect, 4),
                   score=round(W_SHAPE * shape + W_COLOR * color + W_ASPECT * aspect, 4))
        out.append(rec)

    out.sort(key=lambda r: (-r["score"], r["label"]))
    return out


def baseline_info(baseline_png, empty_png):
    """Subject bbox/area of the baseline — used for the contact sheet's reference tile."""
    im = _load(baseline_png)
    if im is None:
        return None
    sub = isolate(im, _plate_for(empty_png, im))
    if sub is None:
        return None
    return {"bbox": tuple(sub["bbox"]), "area": sub["area"], "size": im.size}


# =============================================================================================
# REPORT — contact sheet + markdown/JSON.  Pure functions over files.
# =============================================================================================

TILE = 256          # tile image side (px)
CAPTION_H = 34      # caption strip under each tile
PAD = 8             # gap between tiles
BBOX_MARGIN = 0.15  # fraction of bbox size added on every side before cropping
COLS = 5

BG = (24, 24, 28)
FG = (236, 236, 240)
DIM = (150, 150, 158)
BASELINE_EDGE = (90, 160, 255)
TOP_EDGE = (255, 196, 60)

SUBSCORES = ("shape", "color", "aspect")


def _font(size=13):
    try:
        return ImageFont.load_default(size=size)  # Pillow >= 10.1
    except TypeError:
        return ImageFont.load_default()


def _expand_bbox(bbox, w, h, margin=BBOX_MARGIN):
    """Grow bbox by `margin` per side, keep it square so the crop is not distorted in a square
    tile, and clamp into the frame."""
    x0, y0, x1, y1 = (int(round(v)) for v in bbox)
    x0, x1 = max(0, min(x0, w)), max(0, min(x1, w))
    y0, y1 = max(0, min(y0, h)), max(0, min(y1, h))
    if x1 <= x0 or y1 <= y0:
        return (0, 0, w, h)
    side = max(x1 - x0, y1 - y0) * (1.0 + 2 * margin)
    cx, cy = (x0 + x1) / 2.0, (y0 + y1) / 2.0
    nx0 = int(round(cx - side / 2.0))
    ny0 = int(round(cy - side / 2.0))
    nx1, ny1 = nx0 + int(round(side)), ny0 + int(round(side))
    if nx0 < 0:
        nx1 -= nx0
        nx0 = 0
    if ny0 < 0:
        ny1 -= ny0
        ny0 = 0
    if nx1 > w:
        nx0, nx1 = max(0, nx0 - (nx1 - w)), w
    if ny1 > h:
        ny0, ny1 = max(0, ny0 - (ny1 - h)), h
    return (nx0, ny0, nx1, ny1)


def _tile_image(png, bbox, size=TILE):
    """Crop `png` to its subject bbox (+margin) and scale to FILL a size x size tile.
    Missing/corrupt file -> a placeholder tile (never raises)."""
    tile = Image.new("RGB", (size, size), (44, 44, 50))
    path = _abspath(png)
    if not path or not os.path.exists(path):
        ImageDraw.Draw(tile).text((8, size // 2 - 6), "(no capture)", fill=DIM, font=_font())
        return tile
    try:
        im = Image.open(path).convert("RGB")
    except Exception as e:
        ImageDraw.Draw(tile).text((8, size // 2 - 6), "(bad png)", fill=DIM, font=_font())
        sys.stderr.write("model_match: cannot read %s: %s\n" % (path, e))
        return tile
    crop = im.crop(_expand_bbox(bbox, im.width, im.height)) if bbox else im
    if crop.width and crop.height:
        # Scale to FILL (up as well as down): subjects are often only a few dozen px in the raw
        # shot, and a thumbnail-only sheet would be unreadable.
        s = min(size / crop.width, size / crop.height)
        crop = crop.resize((max(1, int(crop.width * s)), max(1, int(crop.height * s))),
                           Image.LANCZOS)
        tile.paste(crop, ((size - crop.width) // 2, (size - crop.height) // 2))
    return tile


def _ellipsize(draw, text, font, maxw):
    if draw.textlength(text, font=font) <= maxw:
        return text
    while text and draw.textlength(text + "…", font=font) > maxw:
        text = text[:-1]
    return text + "…"


def _caption(draw, x, y, w, title, sub, title_fill=FG):
    f1, f2 = _font(13), _font(11)
    draw.text((x + 4, y + 3), _ellipsize(draw, title, f1, w - 8), fill=title_fill, font=f1)
    if sub:
        draw.text((x + 4, y + 19), _ellipsize(draw, sub, f2, w - 8), fill=DIM, font=f2)


def _fmt(v, nd=3):
    return "-" if v is None else ("%.*f" % (nd, v) if isinstance(v, float) else str(v))


def build_contact_sheet(baseline_png, ranked, out_png,
                        baseline_bbox=None, cols=COLS, tile=TILE, title=None):
    """Render ONE annotated sheet: the baseline tile, then candidate tiles in rank order, each
    cropped to its bbox and captioned with label + scores; rank-1 gets a highlight border."""
    ranked = list(ranked)
    cells = [{"label": "N64 baseline", "png": baseline_png, "bbox": baseline_bbox,
              "_baseline": True}] + ranked

    cols = max(1, min(cols, len(cells)))
    rows = (len(cells) + cols - 1) // cols
    cw, ch = tile + PAD, tile + CAPTION_H + PAD
    head = 26 if title else 0
    sheet = Image.new("RGB", (PAD + cols * cw, PAD + head + rows * ch), BG)
    draw = ImageDraw.Draw(sheet)
    if title:
        draw.text((PAD, PAD), title, fill=FG, font=_font(15))

    for i, c in enumerate(cells):
        col, row = i % cols, i // cols
        x, y = PAD + col * cw, PAD + head + row * ch
        sheet.paste(_tile_image(c.get("png") or c.get("path"), c.get("bbox"), tile), (x, y))

        is_base = c.get("_baseline")
        rank = None if is_base else i  # candidates are 1-based after the baseline
        edge = BASELINE_EDGE if is_base else (TOP_EDGE if rank == 1 else None)
        if edge:
            draw.rectangle([x, y, x + tile - 1, y + tile - 1], outline=edge, width=3)

        label = c.get("label", "?")
        head_txt = label if is_base else "#%d %s" % (rank, label)
        if is_base:
            sub = "reference capture"
        else:
            parts = ["score %s" % _fmt(c.get("score"))]
            parts += ["%s %s" % (k[:3], _fmt(c.get(k), 2))
                      for k in SUBSCORES if c.get(k) is not None]
            sub = "  ".join(parts)
        _caption(draw, x, y + tile, tile, head_txt, sub,
                 title_fill=TOP_EDGE if rank == 1 else FG)

    out = _abspath(out_png)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    sheet.save(out)
    return out


def write_report(ranked, out_md, out_json, baseline=None, meta=None):
    """Write the markdown ranking table and its JSON twin. Returns (abs_md, abs_json)."""
    rows = []
    for i, c in enumerate(list(ranked), 1):
        rec = {"rank": i, "label": c.get("label", "?"), "score": c.get("score")}
        for k in SUBSCORES:
            rec[k] = c.get(k)
        rec["png"] = c.get("png") or c.get("path")
        rec["bbox"] = list(c["bbox"]) if c.get("bbox") else None
        rec["reason"] = c.get("reason", "")
        rows.append(rec)

    md = ["# model_match ranking", ""]
    if baseline:
        md.append("baseline: `%s`" % baseline)
    if meta:
        md += ["%s: %s" % (k, v) for k, v in sorted(meta.items())]
    if baseline or meta:
        md.append("")
    md.append("| rank | label | score | " + " | ".join(SUBSCORES) + " |")
    md.append("|---:|---|---:|" + "---:|" * len(SUBSCORES))
    for r in rows:
        md.append("| %d | %s | %s | %s |" % (
            r["rank"], r["label"], _fmt(r["score"]),
            " | ".join(_fmt(r[k], 2) for k in SUBSCORES)))
    if rows:
        top = rows[0]
        runner = rows[1]["score"] if len(rows) > 1 else None
        gap = (top["score"] - runner) if (top["score"] is not None and runner is not None) else None
        md += ["", "best: **%s** (score %s%s)" % (
            top["label"], _fmt(top["score"]),
            "" if gap is None else ", margin %s over #2" % _fmt(gap))]
    else:
        md += ["", "_no candidates_"]
    md.append("")

    amd, ajson = _abspath(out_md), _abspath(out_json)
    for p in (amd, ajson):
        os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(amd, "w") as f:
        f.write("\n".join(md))
    with open(ajson, "w") as f:
        json.dump({"baseline": baseline, "meta": meta or {}, "ranked": rows}, f, indent=2)
    return amd, ajson


# =============================================================================================
# CLI
# =============================================================================================

def _parse_cmb_list(spec):
    """'1-11' / '1,3,5' / '1-4,9' -> [ints]."""
    out = []
    for part in str(spec).split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part[1:]:
            a, b = part.split("-", 1)
            out += list(range(int(a, 0), int(b, 0) + 1))
        else:
            out.append(int(part, 0))
    return out


def cmd_capture(a):
    """Drive the live game: baseline + empty plate + one shot per candidate CMB."""
    outdir = _abspath(a.out or os.path.join(OUTDIR, "shots"))
    os.makedirs(outdir, exist_ok=True)
    spec = SpawnSpec(actor_id=(a.actor if not a.actor.lower().startswith("0x")
                               and not a.actor.isdigit() else int(a.actor, 0)),
                     entrance=(int(a.entrance, 0) if a.entrance else None),
                     params=int(a.params, 0), nth=a.nth,
                     cam_dist=a.cam_dist, cam_axis=a.cam_axis,
                     scale=a.scale, scale_slot=a.scale_slot, ident_slot=a.ident_slot,
                     settle=a.settle)
    written = {}
    with Session() as s:
        written["baseline"] = s.capture_baseline(spec, os.path.join(outdir, "baseline.png"))
        if not a.no_empty:
            written["empty"] = s.capture_empty(os.path.join(outdir, "empty.png"))
        for n in _parse_cmb_list(a.cmb):
            written["cmb_%d" % n] = s.capture_candidate(
                spec, n, os.path.join(outdir, "cmb_%d.png" % n))
    for k, v in written.items():
        print("%-10s %s" % (k, os.path.relpath(v, REPO)))
    print("\nnow: tools/model_match.py score %s %s %s" % (
        os.path.relpath(written["baseline"], REPO),
        os.path.relpath(written["empty"], REPO) if "empty" in written else "-",
        os.path.join(os.path.relpath(outdir, REPO), "cmb_*.png")))
    return 0


def _candidate_map(args):
    """`label=path` or bare paths (label = basename); globs are expanded."""
    cands = {}
    for a in args:
        label, path = (a.split("=", 1) if "=" in a else (None, a))
        hits = sorted(glob.glob(_abspath(path))) or [path]
        for hp in hits:
            key = label if (label and len(hits) == 1) else \
                os.path.splitext(os.path.basename(hp))[0]
            cands[key] = os.path.relpath(hp, REPO) if os.path.isabs(hp) else hp
    return cands


def cmd_score(a):
    """Rank candidates against a baseline. Pure — never touches the game."""
    empty = None if a.empty in ("-", "none", "") else a.empty
    cands = _candidate_map(a.candidates)
    if not cands:
        sys.stderr.write("model_match: no candidates given\n")
        return 2
    rows = score_candidates(a.baseline, empty, cands)

    print("baseline: %s   empty: %s" % (a.baseline, empty or "(none — local bg estimate)"))
    print("%4s  %6s  %6s  %6s  %6s  %s" % ("rank", "score", "shape", "color", "aspect", "label"))
    for i, r in enumerate(rows, 1):
        note = "   [%s]" % r["reason"] if r["reason"] else ""
        print("%4d  %6.3f  %6.3f  %6.3f  %6.3f  %s%s" % (
            i, r["score"], r["shape"], r["color"], r["aspect"], r["label"], note))

    if a.sheet:
        info = baseline_info(a.baseline, empty)
        out = build_contact_sheet(a.baseline, rows, a.sheet,
                                  baseline_bbox=info["bbox"] if info else None,
                                  cols=a.cols,
                                  title="model_match: %s" % os.path.basename(a.baseline))
        print("sheet:  %s" % os.path.relpath(out, REPO))
    if a.report:
        js = a.json or os.path.splitext(a.report)[0] + ".json"
        md, js = write_report(rows, a.report, js, baseline=a.baseline,
                              meta={"empty": empty or "none"})
        print("report: %s\n        %s" % (os.path.relpath(md, REPO), os.path.relpath(js, REPO)))
    return 0


def main(argv=None):
    p = argparse.ArgumentParser(
        prog="model_match.py", description=__doc__.split("\n\n")[1],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("capture", help="drive the LIVE game to capture baseline/empty/candidates")
    c.add_argument("--actor", required=True, help="actor id (0x...) or model-table name")
    c.add_argument("--params", default="0")
    c.add_argument("--entrance", default=None, help="entrance index to warp to first")
    c.add_argument("--nth", type=int, default=0)
    c.add_argument("--cmb", required=True, help="candidate CMB indices, e.g. 1-11 or 1,3,7")
    c.add_argument("--scale", type=float, default=0.0)
    c.add_argument("--scale-slot", type=int, default=24, dest="scale_slot")
    c.add_argument("--ident-slot", type=int, default=25, dest="ident_slot")
    c.add_argument("--cam-dist", type=float, default=110.0, dest="cam_dist")
    c.add_argument("--cam-axis", type=int, default=0, dest="cam_axis")
    c.add_argument("--settle", type=float, default=0.8,
                   help="seconds between state change and frame dump (raise if candidates look "
                        "off-by-one)")
    c.add_argument("--no-empty", action="store_true", help="skip the background plate")
    c.add_argument("--out", default=None, help="output dir (default scratch/model_match/shots)")
    c.set_defaults(func=cmd_capture)

    s = sub.add_parser("score", help="rank candidate PNGs against a baseline PNG (no game)")
    s.add_argument("baseline")
    s.add_argument("empty", help="empty-plate PNG, or '-' for none")
    s.add_argument("candidates", nargs="+", help="[label=]path.png (globs ok)")
    s.add_argument("--sheet", default=None, help="write the annotated contact sheet here")
    s.add_argument("--report", default=None, help="write the markdown report here (+ .json twin)")
    s.add_argument("--json", default=None, help="explicit JSON path for --report")
    s.add_argument("--cols", type=int, default=COLS)
    s.set_defaults(func=cmd_score)

    a = p.parse_args(argv)
    return a.func(a)


if __name__ == "__main__":
    sys.exit(main())
