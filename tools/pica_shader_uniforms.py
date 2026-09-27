#!/usr/bin/env python3
"""Reconstruct a draw's PICA vertex float-uniform state from a cached command list.

`CmbVShader.shbin` takes almost everything it needs from float uniforms c0..c95,
and the questions about the texture-coordinator mapping switch (`uInvView` at
c76..c78, `TexCoordSlot`/`ShaderMode` at c89, `TexMappingMethod` at c92, the three
`TexMtx` rows) are all questions about those values. The oracle's `vsuni_log`
prints a hand-picked subset of them per draw; this decodes the whole array, so any
uniform is readable offline from a command list already in the cache, with no
emulator run and no new instrumentation.

Register map and packing follow `Azahar/src/video_core/pica/regs_internal.h`
(`vs` block at 0x2b0), `regs_shader.h` (`uniform_setup`) and
`pica_core.cpp::WriteInternalReg`:

* `0x2c0` sets the current uniform index (bits 0-6) and the transfer format
  (bit 31: 0 = float24, 1 = float32).
* `0x2c1..0x2c8` are the eight value words. Each write appends to a queue;
  when the queue is full (3 words in float24 mode, 4 in float32) it decodes into
  `uniforms.f[index]` and the index auto-increments, exactly as
  `ShaderSetup::WriteUniformFloatReg` does. An incomplete trailing queue is
  discarded, because `PackedAttribute::Get` resets rather than pads.

The float24 unpacking and the float32 word order are `PackedAttribute::AsFloat24`
/ `AsFloat32` verbatim, including float32 mode's reversed component order.
"""

from __future__ import annotations

FLOAT_UNIFORM_COUNT = 96  # Uniforms::f, shader_setup.h
VERTEX_UNIFORM_SET = 0x2B0  # ASSERT_REG_POSITION(vs, 0x2b0)
VERTEX_UNIFORM_INDEX = 0x2C0  # uniform_setup: index (bits 0-6) + format (bit 31)
VERTEX_UNIFORM_VALUE = 0x2C1  # uniform_setup: set_value[0]
VERTEX_UNIFORM_VALUE_COUNT = 8
VERTEX_BOOL_UNIFORM = 0x2B0  # ShaderRegs::bool_uniforms
VERTEX_INT_UNIFORM = 0x2B1  # ShaderRegs::int_uniforms[0]

FLOAT24_MODE = 0
FLOAT32_MODE = 1


def float24_from_raw(raw: int) -> float:
    """PICA float24: 1 sign bit, 7 exponent bits biased by 63, 16 mantissa bits."""
    sign = (raw >> 23) & 1
    exponent = (raw >> 16) & 0x7F
    mantissa = raw & 0xFFFF
    if exponent == 0 and mantissa == 0:
        return 0.0
    value = (1.0 + mantissa / 65536.0) * (2.0 ** (exponent - 63))
    return -value if sign else value


def _unpack_float24(buffer: list[int]) -> tuple[float, ...]:
    b0, b1, b2 = buffer[0], buffer[1], buffer[2]
    return (
        float24_from_raw(b2 & 0xFFFFFF),
        float24_from_raw(((b1 & 0xFFFF) << 8) | ((b2 >> 24) & 0xFF)),
        float24_from_raw(((b0 & 0xFF) << 16) | ((b1 >> 16) & 0xFFFF)),
        float24_from_raw(b0 >> 8),
    )


def _unpack_float32(buffer: list[int]) -> tuple[float, ...]:
    import struct
    return tuple(struct.unpack_from("<f", struct.pack("<I", word))[0]
                 for word in reversed(buffer))


class VertexUniforms:
    """The float-uniform array as of one draw, plus whether it was ever written."""

    def __init__(self, values) -> None:
        self._values = list(values)
        self.written = [index for index, value in enumerate(self._values) if value is not None]

    def __len__(self) -> int:
        return len(self._values)

    def vector(self, index: int) -> tuple[float, ...]:
        """Return uniform `index` as four floats, or raise when it was never written."""
        if not 0 <= index < len(self._values):
            raise IndexError(f"vertex float uniform {index} is outside 0..{len(self._values) - 1}")
        value = self._values[index]
        if value is None:
            raise KeyError(f"vertex float uniform c{index} was never written in this capture")
        return value

    def component(self, index: int, component: int) -> float:
        return self.vector(index)[component]

    def get(self, index: int) -> tuple[float, ...] | None:
        """The uniform, or None when this capture never wrote it."""
        if not 0 <= index < len(self._values):
            return None
        return self._values[index]


def decode_vertex_uniforms(writes, end_word: int | None = None) -> VertexUniforms:
    """Replay the uniform-setup stream of a command list into the float array.

    `writes` is the ``(word_index, register, value)`` sequence from
    `pica_command_list.parse_command_writes`. `end_word` stops the replay at a
    draw cursor; None replays the whole list.
    """
    values: list[tuple[float, ...] | None] = [None] * FLOAT_UNIFORM_COUNT
    index = 0
    mode = FLOAT24_MODE
    queue: list[int] = []
    for _word_index, register, value in writes:
        if end_word is not None and _word_index >= end_word:
            break
        if register == VERTEX_UNIFORM_INDEX:
            index = value & 0x7F
            mode = FLOAT32_MODE if (value >> 31) & 1 else FLOAT24_MODE
        elif VERTEX_UNIFORM_VALUE <= register < VERTEX_UNIFORM_VALUE + VERTEX_UNIFORM_VALUE_COUNT:
            queue.append(value)
            full = len(queue) >= 4 if mode == FLOAT32_MODE else len(queue) >= 3
            if not full:
                continue
            unpacked = _unpack_float32(queue) if mode == FLOAT32_MODE else _unpack_float24(queue)
            # Azahar drops a write whose index is past the array instead of wrapping.
            if index < FLOAT_UNIFORM_COUNT:
                values[index] = unpacked
            index += 1
            queue = []
    return VertexUniforms(values)
