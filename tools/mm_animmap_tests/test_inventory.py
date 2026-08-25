"""Tests for N64 object and decomp-XML inventory."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from mm_animmap_inventory import (
    object_to_gar,
    xml_anim_symbols,
    xml_original_names,
    xml_texture_anims,
)
from mm_animmap_paths import REPO


class InventoryTests(unittest.TestCase):
    def test_object_to_gar_filters_candidates_in_preference_order(self) -> None:
        known = {"zelda_dog", "zelda2_dog", "dog"}

        self.assertEqual(
            object_to_gar("object_dog", known),
            ["zelda2_dog", "zelda_dog", "dog"],
        )

    def test_xml_inventory_distinguishes_skeletal_texture_and_absent(self) -> None:
        scratch = Path(REPO) / "scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as raw:
            root = Path(raw)
            objects = root / "2ship/assets/xml/N64_US/objects"
            objects.mkdir(parents=True)
            (objects / "object_dog.xml").write_text(
                '<Animation Name="gDogWaitAnim"/> '
                '<!-- MM3D name is "dog_wait" -->\n'
                '<Animation Name="gDogRemovedAnim"/> '
                '<!-- Original name is "dog_old"; removed in MM3D -->\n'
                '<TextureAnimation Name="gDogEyeAnim"/>\n'
            )

            animations = xml_anim_symbols(str(root))
            annotations = xml_original_names(str(root))
            textures = xml_texture_anims(str(root))

        self.assertEqual(animations["object_dog"], ["gDogWaitAnim", "gDogRemovedAnim"])
        self.assertEqual(annotations["object_dog"]["gDogWaitAnim"], "dog_wait")
        self.assertIsNone(annotations["object_dog"]["gDogRemovedAnim"])
        self.assertEqual(textures["object_dog"], {"gDogEyeAnim"})


if __name__ == "__main__":
    unittest.main()
