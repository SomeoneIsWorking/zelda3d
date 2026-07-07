#!/usr/bin/env python3
"""OoT3D cutscene OP97 ("ccb" ver-3) camera-spline block: parser + evaluator.

Ground truth: FUN_002c5ba0 case 0x97 + FUN_00494600 (reader ctor, requires
payload+4 == 3) + FUN_0032b69c (segment count at +0x10) + FUN_0033cb90
(per-frame evaluator) + FUN_003087a4 (Grezzo keyframe curve). Decomp dumps in
oot3d-decomp/build/decomp/. Derivation: debug_journal/
2026-07-07-title-cs-spot99-format-solved.md.

Layout (all LE, offsets relative to payload = cs op word + 8):
  +0x00 magic "ccb\0"   +0x04 u32 version==3   +0x08 u32 byte len
  +0x10 s32 segment_count   +0x18 s32 seg_offsets[count]
  segment ("caad"):
    +0x04 s32 index   +0x08 s32 start_frame   +0x0C s32 end_frame
    +0x18 f32[3] A default (-> camera slot +0x8C; overwritten by type-1)
    +0x24 f32[3] B default (-> camera slot +0x80; overwritten by type-2)
    +0x30 f32 roll (radians; *10430.378 -> binang)
    +0x3C f32 fov  (radians; *57.29578 -> degrees)
    +0x44 f32 (-> camera slot +0xD0)
    +0x48 s32 track_block_offset (relative to segment)
  track block: +0x04 s32 track_count, +0x08 s32 track_offsets[] (rel. block)
  track: +0x04 u8 type (1=A.xyz 2=B.xyz 3=roll 7=fov 8=+0xD0)
         +0x08/0x0A/0x0C s16 channel offsets (rel. track; 0 = no channel)
  channel (curve): +0x00 u8 interp (1=linear 2=hermite 3=step)
         +0x04 s32 key_count  +0x0C s32 wrap_frame  +0x10 keys
         linear/step key: {s32 frame, f32 value} (8B)
         hermite key: {s32 frame, f32 value, f32 tan_in, f32 tan_out} (16B)
  Curve outputs scale: position *40.0, roll *10430.378, fov *57.29578.
"""
import struct

POS_SCALE = 40.0
RAD_TO_BINANG = 10430.378
RAD_TO_DEG = 57.29578


class Curve:
    def __init__(self, d, off):
        self.interp = d[off]
        self.count, = struct.unpack_from('<i', d, off + 4)
        self.wrap, = struct.unpack_from('<i', d, off + 0xC)
        self.keys = []
        p = off + 0x10
        ksz = 16 if self.interp == 2 else 8
        for _ in range(self.count):
            frame, = struct.unpack_from('<i', d, p)
            val, = struct.unpack_from('<f', d, p + 4)
            if self.interp == 2:
                tin, tout = struct.unpack_from('<ff', d, p + 8)
                self.keys.append((frame, val, tin, tout))
            else:
                self.keys.append((frame, val))
            p += ksz

    def eval(self, t):
        ks = self.keys
        if self.count == 1:
            return ks[0][1]
        idx = 0
        while idx < self.count and not ks[idx][0] >= t:
            idx += 1
        if idx == 0:
            return ks[0][1]
        if idx == self.count:
            return ks[-1][1]
        k0, k1 = ks[idx - 1], ks[idx]
        if self.interp == 3:
            return k0[1]
        if self.interp == 1:
            return k0[1] + (k1[1] - k0[1]) * (t - k0[0]) / (k1[0] - k0[0])
        d = float(k1[0] - k0[0])
        u = (t - k0[0]) / d
        # v0*h00 + v1*h01 + tan_out0*d*h10 + tan_in1*d*h11 (FUN_003087a4 form)
        return (k0[1] + (k0[1] - k1[1]) * (u * 2.0 - 3.0) * u * u
                + (t - k0[0]) * (u - 1.0) * ((u - 1.0) * k0[3] + u * k1[2]))


class Track:
    def __init__(self, d, off):
        self.type = d[off + 4]
        # types 1/2 are Vec3 (three s16 channel offsets at +8/+A/+C);
        # scalar types (3/7/8) only read the channel at +8 (FUN_0033cb90).
        nch = 3 if self.type in (1, 2) else 1
        self.chans = []
        for i in range(nch):
            rel, = struct.unpack_from('<h', d, off + 8 + i * 2)
            self.chans.append(Curve(d, off + rel) if rel else None)
        while len(self.chans) < 3:
            self.chans.append(None)


class Segment:
    def __init__(self, d, off):
        self.index, self.start, self.end = struct.unpack_from('<iii', d, off + 4)
        self.a = list(struct.unpack_from('<3f', d, off + 0x18))
        self.b = list(struct.unpack_from('<3f', d, off + 0x24))
        self.roll, = struct.unpack_from('<f', d, off + 0x30)
        self.fov, = struct.unpack_from('<f', d, off + 0x3C)
        self.d44, = struct.unpack_from('<f', d, off + 0x44)
        tb, = struct.unpack_from('<i', d, off + 0x48)
        self.tracks = []
        if tb:
            base = off + tb
            n, = struct.unpack_from('<i', d, base + 4)
            for i in range(n):
                rel, = struct.unpack_from('<i', d, base + 8 + i * 4)
                self.tracks.append(Track(d, base + rel))


class CamSpline:
    def __init__(self, d, payload_off):
        P = payload_off
        assert d[P:P+3] == b'ccb', 'not a ccb block'
        ver, = struct.unpack_from('<I', d, P + 4)
        assert ver == 3, f'ccb version {ver} != 3'
        cnt, = struct.unpack_from('<i', d, P + 0x10)
        self.segments = []
        for i in range(cnt):
            off, = struct.unpack_from('<i', d, P + 0x18 + i * 4)
            self.segments.append(Segment(d, P + off))

    def segment_for(self, frame):
        # interpreter: seg[8] < frame < seg[0xC] (strict), tracked per csCtx
        for s in self.segments:
            if s.start < frame < s.end:
                return s
        return None

    def eval(self, frame):
        """Returns (A_vec3, B_vec3, roll_binang, fov_deg, d44) world units,
        or None if no segment covers the frame."""
        s = self.segment_for(frame)
        if s is None:
            return None
        a, b = list(s.a), list(s.b)
        roll = int(s.roll * RAD_TO_BINANG)
        fov = s.fov * RAD_TO_DEG
        d44 = s.d44
        for tr in s.tracks:
            vals = [c.eval(float(frame)) if c else None for c in tr.chans]
            if tr.type == 1:
                for j in range(3):
                    if vals[j] is not None:
                        a[j] = vals[j]
            elif tr.type == 2:
                for j in range(3):
                    if vals[j] is not None:
                        b[j] = vals[j]
            elif tr.type == 3 and vals[0] is not None:
                roll = int(vals[0] * RAD_TO_BINANG)
            elif tr.type == 7 and vals[0] is not None:
                fov = vals[0] * RAD_TO_DEG
            elif tr.type == 8 and vals[0] is not None:
                d44 = vals[0]
        return ([v * POS_SCALE for v in a], [v * POS_SCALE for v in b],
                roll, fov, d44)


if __name__ == '__main__':
    import sys
    d = open(sys.argv[1], 'rb').read()
    cs = CamSpline(d, int(sys.argv[2], 16))
    step = int(sys.argv[3]) if len(sys.argv) > 3 else 30
    print("frame  A(x,y,z)                    B(x,y,z)                    roll  fov")
    f = 0
    while f < max(s.end for s in cs.segments):
        r = cs.eval(f)
        if r:
            a, b, roll, fov, _ = r
            print(f"{f:5d}  ({a[0]:8.1f},{a[1]:7.1f},{a[2]:8.1f})  "
                  f"({b[0]:8.1f},{b[1]:7.1f},{b[2]:8.1f})  {roll:6d}  {fov:6.2f}")
        f += step
