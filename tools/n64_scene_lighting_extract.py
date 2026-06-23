#!/usr/bin/env python3
"""
n64_scene_lighting_extract.py — extract N64 per-scene EnvLightSettings from the
SoH .o2r (ZIP-based OTR) archive, producing a JSON dataset parallel to
oot3d-decomp/data/scene_lighting.json.

Usage:
    python3 tools/n64_scene_lighting_extract.py [output.json]

Default output: tools/skeldata/n64_scene_lighting.json

The .o2r archive stores each scene as a serialised resource at a ZIP path like:
  scenes/nonmq/<SceneName>_scene/<SceneName>_scene

The binary layout (SoH OTR v0, little-endian):
  [0..63]      64-byte OTR header (endian byte at 0, type at 4, version at 8, id at 12, ...)
  [64..67]     uint32 commandCount
  per command:
    int32  cmdId       (SceneCommandID enum)
    ...variable payload determined by cmdId...

CommandID enum (from SceneFactory.cpp commandNames[]):
  0  SetStartPositionList   4  SetRoomList          8  SetRoomBehavior
  1  SetActorList           5  SetWind              9  Unused09
  2  SetCsCamera            6  SetEntranceList     10  SetMesh
  3  SetCollisionHeader     7  SetSpecialObjects   11  SetObjectList
 12  SetLightList          13  SetPathways         14  SetTransitionActorList
 15  SetLightingSettings   16  SetTimeSettings     17  SetSkyboxSettings
 18  SetSkyboxModifier     19  SetExitList         20  EndMarker
 21  SetSoundSettings      22  SetEchoSettings     23  SetCutscenes
 24  SetAlternateHeaders   25  SetCameraSettings

EnvLightSettings (per SetLightingSettings entry, total 22 bytes):
  u8[3]  ambientColor
  s8[3]  light1Dir
  u8[3]  light1Color
  s8[3]  light2Dir
  u8[3]  light2Color
  u8[3]  fogColor
  s16    fogNear
  u16    fogFar
"""

from __future__ import annotations
import json
import os
import struct
import sys
import zipfile
from pathlib import Path


OTR_HEADER_SIZE = 64


# ---------------------------------------------------------------------------
# Minimal binary stream
# ---------------------------------------------------------------------------

class Stream:
    def __init__(self, data: bytes, offset: int = 0):
        self.data = data
        self.pos = offset

    def read_int8(self) -> int:
        v = struct.unpack_from("b", self.data, self.pos)[0]
        self.pos += 1
        return v

    def read_uint8(self) -> int:
        v = struct.unpack_from("B", self.data, self.pos)[0]
        self.pos += 1
        return v

    def read_int16(self) -> int:
        v = struct.unpack_from("<h", self.data, self.pos)[0]
        self.pos += 2
        return v

    def read_uint16(self) -> int:
        v = struct.unpack_from("<H", self.data, self.pos)[0]
        self.pos += 2
        return v

    def read_int32(self) -> int:
        v = struct.unpack_from("<i", self.data, self.pos)[0]
        self.pos += 4
        return v

    def read_uint32(self) -> int:
        v = struct.unpack_from("<I", self.data, self.pos)[0]
        self.pos += 4
        return v

    def read_string(self) -> str:
        """Ship::BinaryReader::ReadString — int32 length prefix, then that many chars."""
        n = self.read_int32()
        s = self.data[self.pos:self.pos + n].decode("utf-8", "replace")
        self.pos += n
        return s


# ---------------------------------------------------------------------------
# Per-command parsers (just enough to advance the stream past each cmd)
# ---------------------------------------------------------------------------

def skip_SetStartPositionList(s: Stream) -> None:
    s.read_int32()  # cmdId
    n = s.read_uint32()
    for _ in range(n):
        s.read_uint16()  # id
        s.read_int16(); s.read_int16(); s.read_int16()  # pos
        s.read_int16(); s.read_int16(); s.read_int16()  # rot
        s.read_uint16()  # params

def skip_SetActorList(s: Stream) -> None:
    s.read_int32()
    n = s.read_uint32()
    for _ in range(n):
        s.read_uint16()  # id
        s.read_int16(); s.read_int16(); s.read_int16()  # pos
        s.read_int16(); s.read_int16(); s.read_int16()  # rot
        s.read_uint16()  # params

def skip_SetCsCamera(s: Stream) -> None:
    s.read_int32()
    s.read_int8()   # camSize
    s.read_int32()  # segOffset

def skip_SetCollisionHeader(s: Stream) -> None:
    s.read_int32()
    s.read_string()  # fileName

def skip_SetRoomList(s: Stream) -> None:
    s.read_int32()
    n = s.read_int32()
    for _ in range(n):
        s.read_string()  # fileName
        s.read_int32()   # vromStart
        s.read_int32()   # vromEnd

def skip_SetWind(s: Stream) -> None:
    s.read_int32()
    s.read_int8(); s.read_int8(); s.read_int8(); s.read_uint8()  # windWest/Vert/South/Speed

def skip_SetEntranceList(s: Stream) -> None:
    s.read_int32()
    n = s.read_uint32()
    for _ in range(n):
        s.read_int8(); s.read_int8()  # spawn, room

def skip_SetSpecialObjects(s: Stream) -> None:
    s.read_int32()
    s.read_int8()   # elfMessage
    s.read_int16()  # globalObject

def skip_SetRoomBehavior(s: Stream) -> None:
    s.read_int32()
    s.read_int8()   # gameplayFlags1
    s.read_int32()  # gameplayFlags2

def skip_Unused09(s: Stream) -> None:
    s.read_int32()

def skip_SetMesh(s: Stream) -> None:
    s.read_int32()  # cmdId
    s.read_int8()   # data
    mesh_type = s.read_int8()
    if mesh_type == 1:
        poly_num = 1
    else:
        poly_num = s.read_int8()
    for _ in range(poly_num):
        if mesh_type == 0:
            s.read_int8()    # polyType (unused)
            s.read_string()  # meshOpa
            s.read_string()  # meshXlu
        elif mesh_type == 1:
            s.read_uint8()   # format
            s.read_string(); s.read_string()  # skipped dups
            bg_count = s.read_uint32()
            for _ in range(bg_count):
                s.read_uint16()  # unk_00
                s.read_uint8()   # id
                s.read_string()  # imagePath
                s.read_uint32()  # unk_0C
                s.read_uint32()  # tlut
                s.read_uint16()  # width
                s.read_uint16()  # height
                s.read_uint8()   # fmt
                s.read_uint8()   # siz
                s.read_uint16()  # mode0
                s.read_uint16()  # tlutCount
            s.read_int8()    # polyType (unused)
            s.read_string(); s.read_string()
        elif mesh_type == 2:
            s.read_int8()    # polyType (unused)
            s.read_int16(); s.read_int16(); s.read_int16()  # pos
            s.read_int16()   # unk_06
            s.read_string(); s.read_string()

def skip_SetObjectList(s: Stream) -> None:
    s.read_int32()
    n = s.read_uint32()
    for _ in range(n):
        s.read_uint16()

def skip_SetLightList(s: Stream) -> None:
    s.read_int32()
    n = s.read_uint32()
    for _ in range(n):
        s.read_uint8()   # type
        s.read_int16(); s.read_int16(); s.read_int16()  # pos or dir
        s.read_uint8(); s.read_uint8(); s.read_uint8()  # color
        s.read_uint8()   # drawGlow
        s.read_int16()   # radius

def skip_SetPathways(s: Stream) -> None:
    s.read_int32()
    n = s.read_uint32()
    for _ in range(n):
        s.read_string()

def skip_SetTransitionActorList(s: Stream) -> None:
    s.read_int32()
    n = s.read_uint32()
    for _ in range(n):
        s.read_uint8(); s.read_uint8()   # sides[0]: room, effects
        s.read_uint8(); s.read_uint8()   # sides[1]: room, effects
        s.read_int16()   # id
        s.read_int16(); s.read_int16(); s.read_int16()  # pos
        s.read_int16()   # rotY
        s.read_uint16()  # params

def read_SetLightingSettings(s: Stream) -> list[dict]:
    s.read_int32()  # cmdId
    n = s.read_uint32()
    settings = []
    for _ in range(n):
        amb = [s.read_uint8() for _ in range(3)]
        l1d = [s.read_int8()  for _ in range(3)]
        l1c = [s.read_uint8() for _ in range(3)]
        l2d = [s.read_int8()  for _ in range(3)]
        l2c = [s.read_uint8() for _ in range(3)]
        fog = [s.read_uint8() for _ in range(3)]
        fog_near = s.read_int16()
        fog_far  = s.read_uint16()
        settings.append({
            "ambientColor": amb,
            "light1Dir":    l1d,
            "light1Color":  l1c,
            "light2Dir":    l2d,
            "light2Color":  l2c,
            "fogColor":     fog,
            "fogNear":      fog_near,
            "fogFar":       fog_far,
        })
    return settings

def skip_SetTimeSettings(s: Stream) -> None:
    s.read_int32()
    s.read_int8(); s.read_int8(); s.read_int8()  # hour, minute, timeIncrement

def skip_SetSkyboxSettings(s: Stream) -> None:
    s.read_int32()
    s.read_int8(); s.read_int8(); s.read_int8(); s.read_int8()  # unk, skyboxId, weather, indoors

def skip_SetSkyboxModifier(s: Stream) -> None:
    s.read_int32()
    s.read_int8(); s.read_int8()  # skyboxDisabled, sunMoonDisabled

def skip_SetExitList(s: Stream) -> None:
    s.read_int32()
    n = s.read_uint32()
    for _ in range(n):
        s.read_uint16()

def skip_EndMarker(s: Stream) -> None:
    s.read_int32()  # just the cmdId

def skip_SetSoundSettings(s: Stream) -> None:
    s.read_int32()
    s.read_int8(); s.read_int8(); s.read_int8()  # reverb, natureAmbienceId, seqId

def skip_SetEchoSettings(s: Stream) -> None:
    s.read_int32()
    s.read_int8()  # echo

def skip_SetCutscenes(s: Stream) -> None:
    s.read_int32()
    s.read_string()  # fileName

def skip_SetAlternateHeaders(s: Stream) -> None:
    s.read_int32()
    n = s.read_uint32()
    for _ in range(n):
        s.read_string()

def skip_SetCameraSettings(s: Stream) -> None:
    s.read_int32()
    s.read_int8()   # cameraMovement
    s.read_int32()  # worldMapArea


CMD_DISPATCH = {
    0:  skip_SetStartPositionList,
    1:  skip_SetActorList,
    2:  skip_SetCsCamera,
    3:  skip_SetCollisionHeader,
    4:  skip_SetRoomList,
    5:  skip_SetWind,
    6:  skip_SetEntranceList,
    7:  skip_SetSpecialObjects,
    8:  skip_SetRoomBehavior,
    9:  skip_Unused09,
    10: skip_SetMesh,
    11: skip_SetObjectList,
    12: skip_SetLightList,
    13: skip_SetPathways,
    14: skip_SetTransitionActorList,
    # 15 = SetLightingSettings — handled separately
    16: skip_SetTimeSettings,
    17: skip_SetSkyboxSettings,
    18: skip_SetSkyboxModifier,
    19: skip_SetExitList,
    20: skip_EndMarker,
    21: skip_SetSoundSettings,
    22: skip_SetEchoSettings,
    23: skip_SetCutscenes,
    24: skip_SetAlternateHeaders,
    25: skip_SetCameraSettings,
}

SET_LIGHTING_ID = 15
END_MARKER_ID   = 20


def parse_scene_lighting(data: bytes) -> list[dict] | None:
    """Parse a scene resource binary; return its EnvLightSettings list or None."""
    if len(data) < OTR_HEADER_SIZE + 4:
        return None
    s = Stream(data, OTR_HEADER_SIZE)
    try:
        cmd_count = s.read_uint32()
        for _ in range(cmd_count):
            cmd_id = struct.unpack_from("<i", s.data, s.pos)[0]  # peek
            if cmd_id == SET_LIGHTING_ID:
                return read_SetLightingSettings(s)
            elif cmd_id == END_MARKER_ID:
                break
            handler = CMD_DISPATCH.get(cmd_id)
            if handler is None:
                return None  # unknown cmd — can't advance safely
            handler(s)
    except (struct.error, ValueError, IndexError):
        return None
    return None


def find_o2r() -> str:
    """Return path to the SoH OTR/O2R archive."""
    candidates = [
        Path(__file__).resolve().parents[1] / "Shipwright" / "soh" / "oot.o2r",
        Path(__file__).resolve().parents[1] / "Shipwright" / "build-cmake" / "soh" / "oot.o2r",
    ]
    for c in candidates:
        if c.exists():
            return str(c)
    raise FileNotFoundError(
        "Could not find oot.o2r in Shipwright/soh/ or build-cmake/soh/. "
        "Run a build first or set the path manually."
    )


def main(out_path: str | None = None) -> None:
    archive_path = find_o2r()
    print(f"Reading {archive_path} …")

    if out_path is None:
        skel_dir = Path(__file__).resolve().parent / "skeldata"
        skel_dir.mkdir(parents=True, exist_ok=True)
        out_path = str(skel_dir / "n64_scene_lighting.json")

    results: dict[str, list[dict]] = {}

    with zipfile.ZipFile(archive_path) as zf:
        names = zf.namelist()
        # Scene resources: scenes/{nonmq,shared}/<Name>_scene/<Name>_scene
        # Path has 3 slashes: scenes / {nonmq|shared} / <Name>_scene / <Name>_scene
        # The resource entry has no additional suffix (no room number, no asset suffix).
        scene_paths = [
            n for n in names
            if n.startswith("scenes/") and n.count("/") == 3
               and n.split("/")[3] == n.split("/")[2].replace("_scene", "") + "_scene"
               # i.e. the leaf filename is exactly "<dir>_scene" where dir is the folder name
               # (This filters out room sub-resources like room_0, DL_*, Tex_*, etc.)
        ]
        print(f"Found {len(scene_paths)} scene resources")
        for zpath in sorted(scene_paths):
            # Extract scene name: e.g. "scenes/nonmq/Bmori1_scene/Bmori1_scene" -> "Bmori1_scene"
            fname = zpath.split("/")[-1]  # e.g. "Bmori1_scene"
            # Strip _scene suffix to get canonical name e.g. "Bmori1"
            scene_name = fname[:-6] if fname.endswith("_scene") else fname
            data = zf.read(zpath)
            settings = parse_scene_lighting(data)
            if settings is not None:
                # Normalise name to lower-case to match scene_lighting.json keys
                results[scene_name.lower()] = settings
                print(f"  {scene_name}: {len(settings)} settings")
            else:
                print(f"  {scene_name}: no lighting cmd (skipped)")

    with open(out_path, "w") as f:
        json.dump(results, f, indent=2)

    print(f"\nWrote {len(results)} scenes → {out_path}")


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else None
    main(out)
