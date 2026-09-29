#!/usr/bin/env python3
"""The fill driver must never report a fill it did not perform, or miss a real one.

Two failure modes are pinned here, and both produce a confident wrong number rather than an error:
a per-title target convention that is wrong yields ZERO functions decompiled while the run still
reports success, and an inventory that is not re-read from disk yields a "filled N" total that
double-counts work a previous run already did. The rest of the tests pin the refusals -- a missing
project or a missing inventory must stop the run, not look like a finished job.
"""

from __future__ import annotations

import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from decomp_coverage import looks_decompiled  # noqa: E402
from decomp_fill import (  # noqa: E402
    TitleSpec,
    batches,
    build_invocation,
    missing_targets,
    output_dir_for,
    preflight,
    recovered_count,
    target_file_for,
    title_specs,
)

BODY = "void FUN_00100048(void)\n{\n    gVar = 1;\n}\n"


def write_inventory(game: pathlib.Path, rows: list[tuple[int, int, str]]) -> None:
    decomp = game / "build" / "decomp"
    decomp.mkdir(parents=True, exist_ok=True)
    lines = ["vaddr,size,name"]
    lines += [f"{addr:08x},{size},{name}" for addr, size, name in rows]
    (decomp / "functions.csv").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_recovered(game: pathlib.Path, name: str, addresses: list[int]) -> None:
    decomp = game / "build" / "decomp"
    decomp.mkdir(parents=True, exist_ok=True)
    for addr in addresses:
        (decomp / name.format(addr=addr)).write_text(BODY, encoding="utf-8")


class InventoryTests(unittest.TestCase):
    def test_missing_inventory_is_refused_not_reported_as_nothing_to_do(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(SystemExit) as caught:
                missing_targets(pathlib.Path(tmp))
        self.assertIn("functions.csv", str(caught.exception))

    def test_empty_but_present_inventory_is_also_refused(self) -> None:
        # A header with no rows is not a denominator. Treating it as "0 to do" would report a
        # finished run over a corpus that was never measured.
        with tempfile.TemporaryDirectory() as tmp:
            game = pathlib.Path(tmp)
            write_inventory(game, [])
            with self.assertRaises(SystemExit):
                missing_targets(game)

    def test_recovered_functions_are_excluded_and_counted(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            game = pathlib.Path(tmp)
            write_inventory(game, [(0x10, 8, "FUN_00000010"), (0x20, 8, "FUN_00000020"),
                                   (0x30, 8, "FUN_00000030")])
            write_recovered(game, "fn_0x{addr:08x}.c", [0x20])
            todo, total, already = missing_targets(game)
        self.assertEqual((total, already), (3, 1))
        self.assertEqual(todo, [0x10, 0x30])

    def test_both_naming_conventions_are_recovered(self) -> None:
        # OoT3D writes <vaddr>.c and MM3D writes fn_0x<vaddr>.c. Missing one would silently drop
        # that title's entire recovered set back into the todo list and re-decompile it.
        with tempfile.TemporaryDirectory() as tmp:
            game = pathlib.Path(tmp)
            write_inventory(game, [(0x10, 8, "FUN_00000010"), (0x20, 8, "FUN_00000020")])
            write_recovered(game, "{addr:08x}.c", [0x10])
            write_recovered(game, "fn_0x{addr:08x}.c", [0x20])
            todo, total, already = missing_targets(game)
        self.assertEqual((total, already, todo), (2, 2, []))

    def test_a_header_only_file_is_not_recovered(self) -> None:
        # Ghidra writes a header comment before it knows whether decompilation worked. Counting that
        # as recovered is precisely the inflation the coverage owner exists to prevent.
        with tempfile.TemporaryDirectory() as tmp:
            game = pathlib.Path(tmp)
            write_inventory(game, [(0x10, 8, "FUN_00000010")])
            (game / "build" / "decomp" / "fn_0x00000010.c").write_text(
                "// Function at VA 00100010\n// no body\n", encoding="utf-8")
            todo, _total, already = missing_targets(game)
        self.assertEqual(already, 0)
        self.assertEqual(todo, [0x10])
        self.assertFalse(looks_decompiled("// Function at VA 00100010\n"))


class RecoveredCountTests(unittest.TestCase):
    """Progress must be measured from the tree, because the two scripts report differently."""

    def test_count_reflects_bodies_that_landed_and_ignores_stubs(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            game = pathlib.Path(tmp)
            write_inventory(game, [(0x10, 8, "FUN_00000010"), (0x20, 8, "FUN_00000020"),
                                   (0x30, 8, "FUN_00000030")])
            write_recovered(game, "fn_0x{addr:08x}.c", [0x10, 0x20])
            (game / "build" / "decomp" / "fn_0x00000030.c").write_text(
                "// Function at VA 00100030\n", encoding="utf-8")
            self.assertEqual(recovered_count(game), 2)

    def test_count_is_zero_for_an_inventory_with_no_bodies(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            game = pathlib.Path(tmp)
            write_inventory(game, [(0x10, 8, "FUN_00000010")])
            self.assertEqual(recovered_count(game), 0)

    def test_count_ignores_files_outside_the_inventory(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            game = pathlib.Path(tmp)
            write_inventory(game, [(0x10, 8, "FUN_00000010")])
            write_recovered(game, "fn_0x{addr:08x}.c", [0x10, 0x99])
            self.assertEqual(recovered_count(game), 1)


class BatchingTests(unittest.TestCase):
    def test_batches_partition_without_loss_or_duplication(self) -> None:
        values = list(range(1001))
        chunks = batches(values, 500)
        self.assertEqual([len(c) for c in chunks], [500, 500, 1])
        self.assertEqual([v for c in chunks for v in c], values)

    def test_exact_multiple_does_not_produce_a_trailing_empty_batch(self) -> None:
        self.assertEqual([len(c) for c in batches(list(range(10)), 5)], [5, 5])

    def test_zero_or_negative_batch_size_is_refused(self) -> None:
        for bad in (0, -1):
            with self.assertRaises(ValueError):
                batches([1, 2, 3], bad)


class InvocationTests(unittest.TestCase):
    def _spec(self, mode: str, tmp: str) -> TitleSpec:
        return TitleSpec(
            name="t",
            game_dir=pathlib.Path(tmp) / "game",
            project_dir=pathlib.Path(tmp) / "proj",
            project="p",
            program="code.bin",
            script_dir=pathlib.Path(tmp) / "scripts",
            script="S.py",
            target_mode=mode,
        )

    def test_oot3d_passes_a_target_file_and_mm3d_passes_an_inline_list(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            _cmd, oot_env = build_invocation(self._spec("file", tmp), [0x10, 0x20])
            _cmd, mm_env = build_invocation(self._spec("env-list", tmp), [0x10, 0x20])
        # The file convention must be a PATH to a file; the list convention must be the addresses
        # themselves. Interchanging them is silent: the script finds nothing and writes nothing.
        self.assertTrue(oot_env["DECOMP_TARGETS"].endswith("batch_targets.txt"))
        self.assertEqual(mm_env["DECOMP_TARGETS"], "00000010,00000020")

    def test_mm3d_list_separates_with_commas_and_sorts_nothing(self) -> None:
        # The script splits on commas only, so a space or a 0x prefix silently corrupts a target.
        with tempfile.TemporaryDirectory() as tmp:
            _cmd, env = build_invocation(self._spec("env-list", tmp), [0x30, 0x10])
        self.assertEqual(env["DECOMP_TARGETS"], "00000030,00000010")
        self.assertNotIn(" ", env["DECOMP_TARGETS"])
        self.assertNotIn("0x", env["DECOMP_TARGETS"])

    def test_command_targets_the_named_project_program_and_script(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            command, _env = build_invocation(self._spec("file", tmp), [0x10])
        self.assertEqual(command[0], "analyzeHeadless")
        self.assertIn("-noanalysis", command)
        self.assertIn("-postScript", command)
        self.assertIn("S.py", command)
        self.assertIn("code.bin", command)

    def test_extra_env_from_the_spec_is_preserved(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            spec = TitleSpec(
                name="t",
                game_dir=pathlib.Path(tmp) / "game",
                project_dir=pathlib.Path(tmp) / "proj",
                project="p",
                program="code.bin",
                script_dir=pathlib.Path(tmp) / "scripts",
                script="S.py",
                target_mode="env-list",
                extra_env={"MM3D_DECOMP_OUT": str(pathlib.Path(tmp) / "out")},
            )
            _cmd, env = build_invocation(spec, [0x10])
        self.assertIn("MM3D_DECOMP_OUT", env)

    def test_an_empty_batch_is_refused_rather_than_run(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(ValueError):
                build_invocation(self._spec("file", tmp), [])

    def test_unknown_target_mode_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(ValueError):
                build_invocation(self._spec("carrier-pigeon", tmp), [0x10])

    def test_target_file_is_kept_out_of_the_submodule_tree(self) -> None:
        # The batch list is a per-run artifact. Writing it into the submodule leaves a transient file
        # inside a repo that tracks its own tree, which is a stray uncommitted write per run.
        with tempfile.TemporaryDirectory() as tmp:
            spec = self._spec("file", tmp)
            path = target_file_for(spec)
        self.assertIn("scratch", path.parts)
        self.assertNotIn("build", path.parts)
        self.assertTrue(path.name.startswith("t_"))
        self.assertTrue(path.name.endswith("_batch_targets.txt"))


class PreflightTests(unittest.TestCase):
    def _spec(self, tmp: str, *, project: bool = True, gpr: bool = True, script: bool = True,
              out_env: str | None = None):
        root = pathlib.Path(tmp)
        proj, scripts = root / "proj", root / "scripts"
        proj.mkdir(parents=True, exist_ok=True)
        scripts.mkdir(parents=True, exist_ok=True)
        if gpr:
            (proj / "p.gpr").write_text("", encoding="utf-8")
        if script:
            (scripts / "S.py").write_text("", encoding="utf-8")
        game = root / "game"
        default_out = game / "build" / "decomp"
        env = {"MM3D_DECOMP_OUT": str(out_env or default_out)}
        return TitleSpec(
            name="t", game_dir=game, project_dir=proj, project="p",
            program="code.bin", script_dir=scripts, script="S.py", target_mode="env-list",
            extra_env=env,
        ), project

    def test_a_complete_pipeline_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            spec, keep = self._spec(tmp)
            self.assertTrue(keep)
            self.assertIsNone(preflight(spec))

    def test_missing_project_dir_is_named_in_the_refusal(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            spec, _ = self._spec(tmp)
            object.__setattr__(spec, "project_dir", pathlib.Path(tmp) / "absent")
            with self.assertRaises(SystemExit) as caught:
                preflight(spec)
        self.assertIn("project dir missing", str(caught.exception))

    def test_a_project_directory_without_its_gpr_is_refused(self) -> None:
        # This is the MM3D-shaped failure: a directory that looks like a project tree but holds no
        # analyzed project, which would otherwise run and recover nothing.
        with tempfile.TemporaryDirectory() as tmp:
            spec, _ = self._spec(tmp, gpr=False)
            with self.assertRaises(SystemExit) as caught:
                preflight(spec)
        self.assertIn("p.gpr", str(caught.exception))

    def test_missing_ghidra_script_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            spec, _ = self._spec(tmp, script=False)
            with self.assertRaises(SystemExit) as caught:
                preflight(spec)
        self.assertIn("script missing", str(caught.exception))

    def test_output_landing_outside_the_coverage_tree_is_refused(self) -> None:
        # The silent failure this whole check exists for: a run that decompiles the entire image
        # into a directory coverage never reads, and reports zero recovery for it. It is caught here
        # in a second rather than after twenty minutes of Ghidra.
        with tempfile.TemporaryDirectory() as tmp:
            spec, _ = self._spec(tmp, out_env=str(pathlib.Path(tmp) / "game"))
            with self.assertRaises(SystemExit) as caught:
                preflight(spec)
        self.assertIn("coverage reads", str(caught.exception))

    def test_the_exact_misconfiguration_that_invisible_corpus_wrote_is_refused(self) -> None:
        # MM3D_DECOMP_OUT=<game>/build was a real run's configuration. It is a valid-looking path,
        # the pipeline runs to completion, and coverage reports nothing -- so it must be refused by
        # name, not merely be documented.
        with tempfile.TemporaryDirectory() as tmp:
            spec, _ = self._spec(tmp, out_env=str(pathlib.Path(tmp) / "game" / "build"))
            with self.assertRaises(SystemExit) as caught:
                preflight(spec)
        self.assertIn("coverage reads", str(caught.exception))


class OutputDirTests(unittest.TestCase):
    def test_mm3d_env_value_is_the_output_directory_verbatim(self) -> None:
        # `DumpDecomp.py` uses MM3D_DECOMP_OUT as-is and only appends "/decomp" on the ZELDA3D_REPO
        # fallback. Reading it as "appended" cost a real run: 6465 functions decompiled into
        # mm3d-decomp/build/ that the coverage owner never counted.
        with tempfile.TemporaryDirectory() as tmp:
            game = pathlib.Path(tmp) / "game"
            spec = TitleSpec(
                name="mm3d", game_dir=game, project_dir=game, project="p", program="c",
                script_dir=game, script="S.py", target_mode="env-list",
                extra_env={"MM3D_DECOMP_OUT": str(game / "build" / "decomp")},
            )
            self.assertEqual(output_dir_for(spec), game / "build" / "decomp")

    def test_the_build_directory_alone_is_not_the_output_directory(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            game = pathlib.Path(tmp) / "game"
            spec = TitleSpec(
                name="mm3d", game_dir=game, project_dir=game, project="p", program="c",
                script_dir=game, script="S.py", target_mode="env-list",
                extra_env={"MM3D_DECOMP_OUT": str(game / "build")},
            )
            self.assertEqual(output_dir_for(spec), game / "build")

    def test_oot3d_appends_build_decomp_to_its_repo_value(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            game = pathlib.Path(tmp) / "game"
            spec = TitleSpec(
                name="oot3d", game_dir=game, project_dir=game, project="p", program="c",
                script_dir=game, script="S.py", target_mode="file",
                extra_env={"OOT3D_REPO": str(game)},
            )
            self.assertEqual(output_dir_for(spec), game / "build" / "decomp")

    def test_both_shipped_titles_land_where_coverage_reads(self) -> None:
        for name, spec in title_specs().items():
            self.assertEqual(output_dir_for(spec), spec.game_dir / "build" / "decomp", name)


class ShippedSpecTests(unittest.TestCase):
    """The real titles' configurations, so a rename in a submodule fails here and not mid-run."""

    def test_both_titles_are_configured(self) -> None:
        specs = title_specs()
        self.assertEqual(sorted(specs), ["mm3d", "oot3d"])

    def test_the_two_titles_use_their_own_real_target_conventions(self) -> None:
        specs = title_specs()
        self.assertEqual(specs["oot3d"].target_mode, "file")
        self.assertEqual(specs["mm3d"].target_mode, "env-list")

    def test_the_scripts_exist_in_their_submodules(self) -> None:
        for spec in title_specs().values():
            self.assertTrue((spec.script_dir / spec.script).is_file(), spec.name)

    def test_oot3d_uses_the_fully_analyzed_project(self) -> None:
        # The small `oot3d` project predates full analysis; pointing the fill at it would produce
        # a plausible, much smaller corpus.
        self.assertEqual(title_specs()["oot3d"].project, "oot3d_full")


if __name__ == "__main__":
    unittest.main()
