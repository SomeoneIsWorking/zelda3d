#!/usr/bin/env python3
"""Guard the shipping randomizer hook order across responsibility extractions."""

from pathlib import Path
import re
import unittest


RANDOMIZER_DIR = Path(__file__).resolve().parent
REGISTRAR = RANDOMIZER_DIR / "hook_handlers.cpp"
HANDLER_SOURCE_FILES = (
    "randomizer_actor_lifecycle_hooks.cpp",
    "randomizer_dialogue_policy_hooks.cpp",
    "randomizer_item_delivery_hooks.cpp",
    "randomizer_runtime_lifecycle_hooks.cpp",
    "randomizer_scene_lifecycle_hooks.cpp",
    "randomizer_vanilla_behavior_policy.cpp",
)

REGISTERED_HANDLERS = (
    "RandomizerOnFlagSetHandler",
    "RandomizerOnSceneFlagSetHandler",
    "RandomizerOnPlayerUpdateForRCQueueHandler",
    "RandomizerOnPlayerUpdateForItemQueueHandler",
    "RandomizerOnItemReceiveHandler",
    "RandomizerOnDialogMessageHandler",
    "RandomizerOnVanillaBehaviorHandler",
    "RandomizerOnSceneInitHandler",
    "RandomizerAfterSceneCommandsHandler",
    "RandomizerOnActorInitHandler",
    "RandomizerOnActorUpdateHandler",
    "RandomizerOnPlayerUpdateHandler",
    "RandomizerOnGameFrameUpdateHandler",
    "RandomizerOnSceneSpawnActorsHandler",
    "RandomizerOnPlayDestroyHandler",
    "RandomizerOnExitGameHandler",
    "RandomizerOnKaleidoscopeUpdateHandler",
    "RandomizerOnCuccoOrChickenHatch",
)


class HookRegistrationContractTest(unittest.TestCase):
    def test_shipping_registrar_preserves_hook_order(self) -> None:
        source = REGISTRAR.read_text(encoding="utf-8-sig")
        load_registration = source.index("RegisterGameHook<GameInteractor::OnLoadGame>")
        cursor = load_registration
        for handler in REGISTERED_HANDLERS:
            next_position = source.find(handler, cursor)
            self.assertNotEqual(next_position, -1, f"registrar does not register {handler}")
            cursor = next_position + len(handler)

    def test_each_handler_has_one_shipping_definition(self) -> None:
        sources = "\n".join(
            (RANDOMIZER_DIR / name).read_text(encoding="utf-8-sig")
            for name in HANDLER_SOURCE_FILES
        )
        for handler in REGISTERED_HANDLERS:
            definitions = re.findall(rf"\bvoid\s+{re.escape(handler)}\s*\(", sources)
            self.assertEqual(len(definitions), 1, f"expected one definition for {handler}")


if __name__ == "__main__":
    unittest.main()
