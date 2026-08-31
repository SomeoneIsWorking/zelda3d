from __future__ import annotations

import struct
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from pica_command_list import last_register_write, parse_command_writes
from pica_command_writer_oracle_probe import (
    command_value_records,
    copy_source_value_address,
    harness_environment,
    linear_virtual_address,
    parse_memlog,
    parse_range,
    persist_selected_memlog,
    probe_args,
    selected_writer_record,
    snapshot_config_builder_input,
    snapshot_material_descriptor,
    snapshot_owner_state,
)


def packet(value: int, register: int, extra_count: int = 0, grouped: bool = False) -> list[int]:
    header = register | (extra_count << 20) | (int(grouped) << 31)
    return [value, header]


class CommandWriterTests(unittest.TestCase):
    def test_decodes_grouped_registers(self) -> None:
        words = packet(0x10, 0x140, extra_count=1, grouped=True) + [0x20, 0]
        payload = struct.pack("<4I", *words)
        self.assertEqual(parse_command_writes(payload, 3), [(0, 0x140, 0x10), (2, 0x141, 0x20)])

    def test_selects_last_write_before_draw(self) -> None:
        words = packet(0x1111, 0x1C3) + packet(0x2222, 0x1C3)
        payload = struct.pack("<4I", *words)
        self.assertEqual(last_register_write(payload, 4, 0x1C3), (2, 0x2222))

    def test_translates_fcram_to_linear_virtual_memory(self) -> None:
        self.assertEqual(linear_virtual_address(0x204AF360, 1622), 0x144B0CB8)

    def test_rejects_non_fcram_command_list(self) -> None:
        with self.assertRaisesRegex(ValueError, "outside FCRAM"):
            linear_virtual_address(0x18000000, 0)

    def test_accepts_word_inside_multibyte_memory_write(self) -> None:
        with TemporaryDirectory() as directory:
            path = Path(directory) / "memory.log"
            path.write_text("MW pc=0x00300000 lr=0x00000000 va=0x144b0cb4 sz=8 data=0x0\n")
            self.assertEqual(len(parse_memlog(path, 0x144B0CB8)), 1)

    def test_selects_packet_descriptor_for_config_value(self) -> None:
        records = [
            (
                "MW pc=0x00466e60 lr=0x00466e20 va=0x145913d8 sz=4 "
                "data=0x0000000080000400 r0=0x0821e96c r4=0x08210000 r4p8=0x0821e964 "
                "r4p10=0x00000018 r4p14=0x00000001 sr4=0x08200000 sr4p0=0x00100000 "
                "sr4p4=0x08201000 sr4p5c=0x08210000 sr4p6c=0x08220000 sr4t14=0x00450000 "
                "sr4t20=0x00450010 sr4t24=0x00450020"
            )
        ]
        self.assertEqual(selected_writer_record(records, 0x80000400)["r4"], 0x08210000)

    def test_recovers_copy_source_address_from_loaded_register(self) -> None:
        writer = {
            "pc": 0x00371758,
            "r1": 0x005B31BC,
            "r7": 0x01020304,
            "r8": 0x80000400,
            "r9": 0x05060708,
            "r10": 0x090A0B0C,
        }
        self.assertEqual(copy_source_value_address(writer, 0x80000400), 0x005B31B0)

    def test_keeps_only_exact_command_value_records(self) -> None:
        records = [
            "MW pc=0x00000001 data=0x000000003f800000 r4=0x00000001",
            "MW pc=0x00000002 data=0x0000000080000400 r4=0x00000001",
        ]
        self.assertEqual(command_value_records(records, 0x80000400), records[1:])

    def test_rejects_ambiguous_copy_source_value(self) -> None:
        writer = {
            "pc": 0x00371758,
            "r1": 0x005B31BC,
            "r7": 0x80000400,
            "r8": 0x80000400,
            "r9": 0x05060708,
            "r10": 0x090A0B0C,
        }
        with self.assertRaisesRegex(RuntimeError, "2 source registers"):
            copy_source_value_address(writer, 0x80000400)

    def test_snapshots_config_builder_input_at_exact_template_store(self) -> None:
        record = {
            "pc": 0x0040CFE4,
            "r10": 0x081D1638,
            "r10b": 0x081D1538,
            "r10bp0": 0x00000000,
            "r10bp164": 0x01020304,
            "r10bp168": 0x05060708,
            "r10bp16c": 0x090A0B0C,
            "r10bp170": 0x0D0E0F10,
            "r10bp174": 0x11121314,
            "r10bp178": 0x15161718,
            "r10bp17c": 0x191A1B1C,
            "r10bp180": 0x1D1E1F20,
            "r10bp184": 0x21222324,
            "r10bp188": 0x25262728,
            "r10bp18c": 0x292A2B2C,
            "r10bp190": 0x2D2E2F30,
        }
        snapshot = snapshot_config_builder_input(record)
        self.assertEqual(snapshot["address"], "0x081d1538")
        self.assertEqual(snapshot["words"]["0x184"], "0x21222324")

    def test_rejects_mismatched_config_builder_base(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "does not match r10"):
            snapshot_config_builder_input(
                {
                    "pc": 0x0040CFE4,
                    "r10": 0x081D1638,
                    "r10b": 0x081D153C,
                    "r10bp0": 0,
                    "r10bp164": 0,
                    "r10bp168": 0,
                    "r10bp16c": 0,
                    "r10bp170": 0,
                    "r10bp174": 0,
                    "r10bp178": 0,
                    "r10bp17c": 0,
                    "r10bp180": 0,
                    "r10bp184": 0,
                    "r10bp188": 0,
                    "r10bp18c": 0,
                    "r10bp190": 0,
                }
            )

    def test_snapshots_material_descriptor_at_exact_bind(self) -> None:
        descriptor = snapshot_material_descriptor(
            {
                "pc": 0x004C6374,
                "r1": 0x08EEC8D8,
                "r1p10": 0x62C984C1,
                "r1p14": 0x00000001,
                "r1p18": 0x62B20000,
                "r1p1c": 0x62C10000,
                "r1p20": 0x00010001,
                "r1p24": 0x62A10001,
                "r1p28": 0x40000000,
            }
        )
        self.assertEqual(descriptor["address"], "0x08eec8d8")
        self.assertEqual(descriptor["words"]["0x24"], "0x62a10001")

    def test_snapshots_exact_store_dispatcher_fields(self) -> None:
        state = snapshot_owner_state(
            {
                "r0": 0x0821E96C,
                "r4": 0x08210000,
                "r4p8": 0x0821E964,
                "r4p10": 0x18,
                "r4p14": 1,
                "sr4": 0x08200000,
                "sr4p0": 0x00100000,
                "sr4p4": 0x08201000,
                "sr4p5c": 0x08210000,
                "sr4p6c": 0x08220000,
                "sr4t14": 0x00450000,
                "sr4t20": 0x00450010,
                "sr4t24": 0x00450020,
            }
        )
        self.assertEqual(state["packet_descriptor"]["source_pointer"], "0x0821e964")
        self.assertEqual(state["source_value_address"], "0x0821e968")
        self.assertEqual(state["virtual_slots"]["setup_c"], "0x00450020")

    def test_interpreter_environment_enables_direct_write_logging(self) -> None:
        environment = harness_environment("interpreter", (0x14480000, 0x145A0000), Path("memory.log"))
        self.assertEqual(environment["SOH3D_HARNESS_DISABLE_FASTMEM"], "1")
        self.assertEqual(environment["SOH3D_CPU_INTERPRETER"], "1")
        self.assertEqual(environment["SOH3D_MEMLOG_RANGES"], "0x14480000:0x145a0000")

    def test_environment_adds_packet_source_range(self) -> None:
        environment = harness_environment(
            "interpreter", (0x14480000, 0x145A0000), Path("memory.log"), (0x0821E000, 0x08220000)
        )
        self.assertEqual(
            environment["SOH3D_MEMLOG_RANGES"], "0x14480000:0x145a0000,0x0821e000:0x08220000"
        )

    def test_environment_adds_exact_write_watch(self) -> None:
        environment = harness_environment(
            "interpreter", (0x14480000, 0x145A0000), Path("memory.log"), watch_address=0x005B31BC
        )
        self.assertEqual(environment["SOH3D_MEMLOG_RANGES"], "0x14480000:0x145a0000,0x005b31bc:0x005b31c0")

    def test_environment_adds_exact_renderer_state_watch(self) -> None:
        environment = harness_environment(
            "interpreter",
            (0x14480000, 0x145A0000),
            Path("memory.log"),
            watch_address=0x005B31BC,
            state_watch_address=0x081D1538,
        )
        self.assertEqual(
            environment["SOH3D_MEMLOG_RANGES"],
            "0x14480000:0x145a0000,0x005b31bc:0x005b31c0,0x081d1538:0x081d153c",
        )

    def test_state_watch_schema_is_part_of_the_cache_key(self) -> None:
        args = probe_args(
            draw=4,
            register=0x1C3,
            label="fixture",
            entrance=0x30D,
            daytime=0x6000,
            settle_frames=180,
            linear_range=(0x14480000, 0x145A0000),
            cpu_mode="interpreter",
            source_range=None,
            watch_address=0x005B31B4,
            state_watch_address=0x081D1538,
        )
        self.assertEqual(args["state_watch_trace_version"], 2)

    def test_trace_environment_can_target_one_command_word(self) -> None:
        environment = harness_environment("interpreter", (0x144B0CB8, 0x144B0CBC), Path("memory.log"))
        self.assertEqual(environment["SOH3D_MEMLOG_RANGES"], "0x144b0cb8:0x144b0cbc")

    def test_rejects_invalid_range(self) -> None:
        with self.assertRaisesRegex(ValueError, "--source-range must be non-empty"):
            parse_range("0x10:0x10", "--source-range")

    def test_persists_only_selected_memory_records(self) -> None:
        with TemporaryDirectory() as directory:
            path = Path(directory) / "selected.log"
            persist_selected_memlog(path, ["MW first", "MW first"], ["MW second"])
            self.assertEqual(path.read_text().splitlines(), ["# Exact writer records selected from the oracle memory log.", "MW first", "MW second"])

    def test_persists_empty_watch_result_explicitly(self) -> None:
        with TemporaryDirectory() as directory:
            path = Path(directory) / "selected.log"
            persist_selected_memlog(path, ["MW writer"], watch_address=0x005B31BC, watch_count=0)
            self.assertEqual(
                path.read_text().splitlines(),
                [
                    "# Exact writer records selected from the oracle memory log.",
                    "# Watch 0x005b31bc: 0 matching record(s).",
                    "MW writer",
                ],
            )

    def test_rejects_unknown_cpu_mode(self) -> None:
        with self.assertRaisesRegex(ValueError, "unsupported CPU mode"):
            harness_environment("unknown", (0x14480000, 0x145A0000), Path("memory.log"))
