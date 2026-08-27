"""Regression coverage for the MM3D CMB v10 layout gates."""

from __future__ import annotations

import struct
import unittest

import cmb
from mm_player_cmb_dump import parse_args, summarize_mesh_ids


def _pack_into(blob: bytearray, offset: int, fmt: str, *values: object) -> None:
    struct.pack_into("<" + fmt, blob, offset, *values)


def _synthetic_v10_cmb() -> bytes:
    blob = bytearray(0x500)
    blob[:4] = b"cmb "
    _pack_into(blob, 0x08, "I", 10)
    blob[0x10:0x17] = b"mm_test"

    skl_ptr = 0x50
    vatr_ptr = 0x180
    sklm_ptr = 0x200
    _pack_into(blob, 0x24, "I", skl_ptr)
    _pack_into(blob, 0x28, "I", 0x160)  # v10-only qtrs slot
    _pack_into(blob, 0x2C, "I", 0)  # mats
    _pack_into(blob, 0x30, "I", 0)  # tex
    _pack_into(blob, 0x34, "I", sklm_ptr)
    _pack_into(blob, 0x3C, "I", vatr_ptr)
    _pack_into(blob, 0x40, "I", 0x480)  # idx
    _pack_into(blob, 0x44, "I", 0x480)  # texdata

    blob[skl_ptr : skl_ptr + 4] = b"skl "
    _pack_into(blob, skl_ptr + 8, "I", 2)
    bone0 = skl_ptr + 0x10
    bone1 = bone0 + 0x2C
    _pack_into(blob, bone0, "BBh", 0, 0, -1)
    _pack_into(blob, bone0 + 4, "3f", 1.0, 1.0, 1.0)
    _pack_into(blob, bone1, "BBh", 1, 0, 0)
    _pack_into(blob, bone1 + 4, "3f", 1.0, 1.0, 1.0)
    _pack_into(blob, bone1 + 0x1C, "3f", 10.0, 20.0, 30.0)

    blob[vatr_ptr : vatr_ptr + 4] = b"vatr"

    blob[sklm_ptr : sklm_ptr + 4] = b"sklm"
    _pack_into(blob, sklm_ptr + 8, "II", 0x20, 0x80)
    mshs_ptr = sklm_ptr + 0x20
    blob[mshs_ptr : mshs_ptr + 4] = b"mshs"
    _pack_into(blob, mshs_ptr + 8, "I", 2)
    mesh0 = mshs_ptr + 0x10
    mesh1 = mesh0 + 0x0C
    _pack_into(blob, mesh0, "HBB", 0, 1, 7)
    _pack_into(blob, mesh1, "HBB", 0, 2, 9)

    shp_ptr = sklm_ptr + 0x80
    blob[shp_ptr : shp_ptr + 4] = b"shp "
    _pack_into(blob, shp_ptr + 8, "I", 1)
    _pack_into(blob, shp_ptr + 0x10, "H", 0x20)
    sepd_ptr = shp_ptr + 0x20
    blob[sepd_ptr : sepd_ptr + 4] = b"sepd"
    _pack_into(blob, sepd_ptr + 8, "H", 0)
    return bytes(blob)


class MmCmbV10Tests(unittest.TestCase):
    def test_exact_member_gate_parses_required_mesh_ids(self) -> None:
        args = parse_args(
            ["mm_player_cmb_dump.py", "archive", "member", "--require-mids", "3", "17"]
        )
        self.assertEqual(args.archive_path, "archive")
        self.assertEqual(args.cmb_member_path, "member")
        self.assertEqual(args.require_mids, [3, 17])

    def test_v10_qtrs_bone_and_mesh_strides(self) -> None:
        model = cmb.Cmb(_synthetic_v10_cmb())

        self.assertEqual(model.sklm_ptr, 0x200)
        self.assertEqual(
            [(bone.id, bone.parent) for bone in model.bones], [(0, -1), (1, 0)]
        )
        self.assertEqual(model.bones[1].trans, (10.0, 20.0, 30.0))
        self.assertEqual(
            [
                (mesh.sepd_index, mesh.material_index, mesh.mesh_id)
                for mesh in model.meshes
            ],
            [(0, 1, 7), (0, 2, 9)],
        )

        summaries = summarize_mesh_ids(model)
        self.assertEqual(sorted(summaries), [7, 9])
        self.assertEqual(summaries[7].mesh_count, 1)
        self.assertEqual(summaries[7].materials, {1})
        self.assertEqual(summaries[9].materials, {2})


if __name__ == "__main__":
    unittest.main()
