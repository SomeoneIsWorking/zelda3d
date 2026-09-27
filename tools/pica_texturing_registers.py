#!/usr/bin/env python3
"""PICA200 texturing register field map, for decoding a captured command list.

A cached OoT3D command list (`tools/pica_command_list.py` writes) records which
32-bit value each PICA register last held before a draw. Deciding what a value
*means* needs the register map, and getting that map wrong is silent: a field read
from the wrong word still produces a plausible number. So it lives here, once, with
the Azahar static asserts it comes from cited next to every entry, and it is checked
against a real capture in `tools/test_pica_texturing_registers.py`.

Source of truth: `Azahar/src/video_core/pica/regs_internal.h` (the
`ASSERT_REG_POSITION` lines) and `Azahar/src/video_core/pica/regs_texturing.h`
(`struct TextureConfig`). The map is Azahar's layout, which is 3dbrew's PICA200
register layout.

`TextureConfig` is five words: border colour, height/width, filter/wrap/type, LOD,
address. For unit 0 that is PICA registers 0x081..0x085, with the cube-map
addresses, the shadow LOD and the format/fragment-lighting word after it.
"""

from __future__ import annotations

from dataclasses import dataclass

TEXTURING_BASE = 0x80  # ASSERT_REG_POSITION(texturing, 0x80)
MAIN_CONFIG = 0x80  # texture{0,1,2}_enable, texture2_use_coord1
TEXTURE0 = 0x81  # TextureConfig for unit 0
TEXTURE0_FORMAT = 0x8E  # + fragment_lighting_enable in the next word
FRAGMENT_LIGHTING_ENABLE = 0x8F
TEXTURE1 = 0x91
TEXTURE1_FORMAT = 0x96
TEXTURE2 = 0x99
TEXTURE2_FORMAT = 0x9E

# Word offsets inside one TextureConfig.
WORD_BORDER_COLOR = 0
WORD_SIZE = 1
WORD_FILTER = 2
WORD_LOD = 3
WORD_ADDRESS = 4

TEXTURE_TYPE_NAMES = {
    0: "Texture2D",
    1: "TextureCube",
    2: "Shadow2D",
    3: "Projection2D",
    4: "ShadowCube",
    5: "Disabled",
}

WRAP_MODE_NAMES = {
    0: "ClampToEdge",
    1: "ClampToBorder",
    2: "Repeat",
    3: "MirroredRepeat",
    4: "ClampToEdge2",
    5: "ClampToBorder2",
    6: "Repeat2",
    7: "Repeat3",
}

FILTER_NAMES = {0: "Nearest", 1: "Linear"}


@dataclass(frozen=True)
class TextureConfig:
    """One decoded `TextureConfig`, plus the register word it was read from."""

    base: int
    border_color: int
    width: int
    height: int
    mag_filter: str
    min_filter: str
    wrap_t: str
    wrap_s: str
    mip_filter: str
    texture_type: int
    lod_bias: int
    physical_address: int

    @property
    def type_name(self) -> str:
        return TEXTURE_TYPE_NAMES.get(self.texture_type, f"Unknown({self.texture_type})")

    @property
    def is_projective(self) -> bool:
        """True when PICA divides the texture coordinate by the vertex `w`."""
        return self.texture_type == 3


def _last_writes(writes, end_word: int) -> dict[int, int]:
    """Reduce (word_index, register, value) writes to the last value per register."""
    latest: dict[int, tuple[int, int]] = {}
    for word_index, register, value in writes:
        if word_index >= end_word:
            continue
        if register not in latest or word_index > latest[register][0]:
            latest[register] = (word_index, value)
    return {register: value for register, (_word, value) in latest.items()}


def decode_texture0(writes, end_word: int) -> TextureConfig | None:
    """Decode unit 0's texture config from the writes preceding `end_word`.

    Returns None when the capture never wrote the config's filter word, which means
    "not observed" — never a default that reads as a real answer.
    """
    registers = _last_writes(writes, end_word)
    if TEXTURE0 + WORD_FILTER not in registers:
        return None
    filter_word = registers[TEXTURE0 + WORD_FILTER]
    size_word = registers.get(TEXTURE0 + WORD_SIZE, 0)
    address_word = registers.get(TEXTURE0 + WORD_ADDRESS, 0)
    return TextureConfig(
        base=TEXTURE0,
        border_color=registers.get(TEXTURE0 + WORD_BORDER_COLOR, 0),
        # TextureConfig's size word is `height` in bits 0..10 and `width` in bits
        # 16..26 (regs_texturing.h). The oracle's identity line prints them
        # `WxH`, so decoding these the other way round still produces two plausible
        # numbers that merely disagree.
        width=(size_word >> 16) & 0x7FF,
        height=size_word & 0x7FF,
        mag_filter=FILTER_NAMES[(filter_word >> 1) & 1],
        min_filter=FILTER_NAMES[(filter_word >> 2) & 1],
        wrap_t=WRAP_MODE_NAMES[(filter_word >> 8) & 7],
        wrap_s=WRAP_MODE_NAMES[(filter_word >> 12) & 7],
        mip_filter=FILTER_NAMES[(filter_word >> 24) & 1],
        texture_type=(filter_word >> 28) & 7,
        lod_bias=registers.get(TEXTURE0 + WORD_LOD, 0) & 0x1FFF,
        physical_address=(address_word & 0x0FFFFFFF) * 8,
    )


def decode_main_config(writes, end_word: int) -> dict[str, int] | None:
    """Decode the texturing block's enable bits, or None when never written."""
    registers = _last_writes(writes, end_word)
    if MAIN_CONFIG not in registers:
        return None
    value = registers[MAIN_CONFIG]
    return {
        "texture0_enable": value & 1,
        "texture1_enable": (value >> 1) & 1,
        "texture2_enable": (value >> 2) & 1,
        "texture3_coordinates": (value >> 8) & 3,
        "texture3_enable": (value >> 10) & 1,
        "texture2_use_coord1": (value >> 13) & 1,
    }


def decode_fragment_lighting_enable(writes, end_word: int) -> int | None:
    """The texturing block's fragment-lighting enable, or None when never written."""
    registers = _last_writes(writes, end_word)
    value = registers.get(FRAGMENT_LIGHTING_ENABLE)
    return None if value is None else value & 1
