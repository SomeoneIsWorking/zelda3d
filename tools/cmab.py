#!/usr/bin/env python3
"""CMAB (CTR Material Animation Binary) parser — OoT3D material/texcoord animations.

Layout reverse-engineered from noclip.website (src/OcarinaOfTime3D/cmab.ts); the OoT3D
("Ocarina") subvariant has 32-bit track headers. We only need the texcoord-translation
animations (AnimationType.Translation) that scroll the sky `kumo` cloud bands, but the parser
is complete enough to dump any cmab.

A Translation entry carries up to two tracks: track[0] = translate-U, track[1] = translate-V,
each a piecewise-linear/hermite curve over the animation's frame `duration` (loop mode Repeat).
For the BlueSky.zar cloud cmabs the curve is a single linear segment (0,0)->(duration,delta),
i.e. a constant scroll rate of delta/duration UV per frame.

Usage:
    python3 tools/cmab.py                      # dump the BlueSky.zar kumo cmabs
    python3 tools/cmab.py /kankyo/BlueSky.zar misc/fine_kumo_a.cmab
"""
import struct
from dataclasses import dataclass, field
from typing import Optional

# AnimationType (mmad +0x04)
ANIM_TRANSLATION = 0x01
ANIM_TEXTURE_PALETTE = 0x02
ANIM_DIFFUSE = 0x03
ANIM_CONST_COLOR = 0x04
ANIM_ROTATION = 0x05
ANIM_SCALE = 0x06

# AnimationTrackType (track +0x00)
TRACK_LINEAR = 0x01
TRACK_HERMITE = 0x02
TRACK_INTEGER = 0x03

_ANIM_NAMES = {
    1: "Translation", 2: "TexturePalette", 3: "DiffuseColor", 4: "ConstColor",
    5: "Rotation", 6: "Scale", 7: "AmbientColor", 8: "Spec0", 9: "Spec1", 10: "Emission",
}


@dataclass
class Keyframe:
    time: float
    value: float
    tangent_in: float = 0.0
    tangent_out: float = 0.0


@dataclass
class Track:
    type: int
    time_start: float
    time_end: float
    frames: list = field(default_factory=list)

    def sample(self, frame: float) -> float:
        """Sample the curve at `frame` (matches noclip's sampler)."""
        fr = self.frames
        if not fr:
            return 0.0
        idx1 = next((i for i, k in enumerate(fr) if frame < k.time), -1)
        if idx1 == 0:
            return fr[0].value
        if idx1 < 0:
            return fr[-1].value
        k0, k1 = fr[idx1 - 1], fr[idx1]
        if self.type == TRACK_INTEGER:
            return k0.value
        t = (frame - k0.time) / (k1.time - k0.time)
        if self.type == TRACK_HERMITE and (k1.time - k0.time) != 1:
            length = k1.time - k0.time
            s0 = k0.tangent_out * length
            s1 = k1.tangent_in * length
            # Hermite basis
            t2, t3 = t * t, t * t * t
            h00 = 2 * t3 - 3 * t2 + 1
            h10 = t3 - 2 * t2 + t
            h01 = -2 * t3 + 3 * t2
            h11 = t3 - t2
            return h00 * k0.value + h10 * s0 + h01 * k1.value + h11 * s1
        return k0.value + (k1.value - k0.value) * t

    def linear_rate(self) -> Optional[float]:
        """If this is a single linear/integer ramp from frame 0, return (value_end-value_start)
        per frame; else None. (The cloud cmabs are exactly this.)"""
        if len(self.frames) == 2 and self.frames[0].time == 0:
            dt = self.frames[1].time - self.frames[0].time
            if dt > 0:
                return (self.frames[1].value - self.frames[0].value) / dt
        return None


@dataclass
class AnimEntry:
    anim_type: int
    material_index: int
    channel_index: int
    tracks: list  # index 0 = U/x, 1 = V/y (Translation/Scale)


@dataclass
class Cmab:
    duration: int
    loop_mode: int
    entries: list


def _parse_track(buf: bytes, off: int) -> Optional[Track]:
    typ, nkf, tstart, tend = struct.unpack_from("<IIII", buf, off)
    if nkf == 0:
        return None
    frames = []
    idx = off + 0x10
    for _ in range(nkf):
        if typ == TRACK_HERMITE:
            time, value, ti, to = struct.unpack_from("<Ifff", buf, idx)
            frames.append(Keyframe(time, value, ti, to))
            idx += 0x10
        else:  # Linear / Integer
            time, value = struct.unpack_from("<If", buf, idx)
            frames.append(Keyframe(time, value))
            idx += 0x08
    return Track(typ, tstart, tend, frames)


def _parse_mmad(buf: bytes, off: int) -> AnimEntry:
    assert buf[off:off + 4] == b"mmad", buf[off:off + 4]
    anim_type, material_index = struct.unpack_from("<II", buf, off + 0x04)
    channel_index = 0
    track_table = off + 0x0C
    if anim_type in (ANIM_TRANSLATION, ANIM_ROTATION, ANIM_SCALE, ANIM_TEXTURE_PALETTE,
                     ANIM_CONST_COLOR):
        channel_index = struct.unpack_from("<I", buf, off + 0x0C)[0]
        track_table += 0x04
    ntracks = {ANIM_TRANSLATION: 2, ANIM_SCALE: 2, ANIM_ROTATION: 1,
               ANIM_TEXTURE_PALETTE: 1}.get(anim_type, 4)
    tracks = [None] * ntracks
    for i in range(ntracks):
        track_offs = struct.unpack_from("<H", buf, track_table + i * 2)[0]
        if track_offs != 0:
            tracks[i] = _parse_track(buf, off + track_offs)
    return AnimEntry(anim_type, material_index, channel_index, tracks)


def parse(buf: bytes) -> Cmab:
    assert buf[0:4] == b"cmab", "not a cmab"
    subversion = struct.unpack_from("<I", buf, 0x04)[0]
    assert subversion == 1, subversion
    assert struct.unpack_from("<I", buf, 0x10)[0] == 1
    assert struct.unpack_from("<I", buf, 0x14)[0] == 0x20
    assert struct.unpack_from("<I", buf, 0x20)[0] == 0xFFFFFFFF
    duration = struct.unpack_from("<I", buf, 0x24)[0]
    loop_mode = struct.unpack_from("<I", buf, 0x28)[0]
    mads = 0x34
    assert buf[mads:mads + 4] == b"mads"
    nanim = struct.unpack_from("<I", buf, mads + 0x04)[0]
    entries = []
    tbl = mads + 0x08
    for _ in range(nanim):
        rel = struct.unpack_from("<I", buf, tbl)[0]
        entries.append(_parse_mmad(buf, mads + rel))
        tbl += 0x04
    return Cmab(duration, loop_mode, entries)


def texcoord_translation_rate(cmab: Cmab):
    """Return (du_per_frame, dv_per_frame) summed over the channel-0 translation entry, the
    primary cloud-band scroll. Falls back to 0 if absent."""
    du = dv = 0.0
    for e in cmab.entries:
        if e.anim_type != ANIM_TRANSLATION:
            continue
        ru = e.tracks[0].linear_rate() if e.tracks[0] else None
        rv = e.tracks[1].linear_rate() if (len(e.tracks) > 1 and e.tracks[1]) else None
        # channel 0 = the base cloud layer; that is the one our single-texcoord renderer samples.
        if e.channel_index == 0:
            du += ru or 0.0
            dv += rv or 0.0
    return du, dv


def _dump(name: str, buf: bytes):
    c = parse(buf)
    print(f"=== {name} ({len(buf)} B) duration={c.duration} loop={c.loop_mode} "
          f"entries={len(c.entries)} ===")
    for e in c.entries:
        print(f"  {_ANIM_NAMES.get(e.anim_type, e.anim_type)} mat={e.material_index} "
              f"ch={e.channel_index}")
        for ti, t in enumerate(e.tracks):
            if t is None:
                continue
            axis = "UVZW"[ti] if ti < 4 else str(ti)
            rate = t.linear_rate()
            kfs = ", ".join(f"({k.time:g}:{k.value:g})" for k in t.frames)
            rstr = f"  rate={rate:+.6g}/frame" if rate is not None else ""
            print(f"    track[{axis}] type={t.type} [{kfs}]{rstr}")
    du, dv = texcoord_translation_rate(c)
    wrap = (1.0 / abs(du)) if du else float("inf")
    print(f"  -> ch0 scroll rate: dU={du:+.6g} dV={dv:+.6g} per frame "
          f"(full U wrap ~{wrap:.0f} frames)")
    return c


if __name__ == "__main__":
    import sys, os
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from ctr_romfs import CtrRom
    from zar import Zar
    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    if len(sys.argv) >= 3:
        z = Zar(rom.read(rom.get(sys.argv[1])))
        f = [x for x in z.files if x.name == sys.argv[2]][0]
        _dump(sys.argv[2], z.read(f))
    else:
        z = Zar(rom.read(rom.get("/kankyo/BlueSky.zar")))
        for f in z.files:
            if f.name.endswith(".cmab"):
                _dump(f.name, z.read(f))
