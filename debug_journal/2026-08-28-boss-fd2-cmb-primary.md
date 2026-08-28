# 2026-08-28 — BossFd2 CMB PRIMARY / vertex-light audit

## Named 3DS mechanism

`/CmbVShader.shbin` owns CMB PRIMARY generation. Static disassembly of words 14–61, 76–120,
and the palette helper at 266–274 establishes this order:

1. apply the CMB attribute scales to position, normal, and optional color;
2. blend position and normal through the matrix palette;
3. normalize the skinned normal;
4. transform that normal by the draw matrix;
5. accumulate `matAmbient*lightAmbient + max(dot(N,-lightDir),0)*matDiffuse*lightDiffuse`
   for every enabled slot;
6. multiply by the optional vertex color, then let PICA saturate the output register per vertex.

The actor bank is the already-recovered two-slot arrangement: `+D/light2Color/sceneAmbient` and
`-D/light1Color/zeroAmbient`. Caller draw bit 31 chooses actor versus scene bank; it does not replace
the CMB material's `vertex_lighting` bit.

## Asset discriminators

Direct `valbasiagnd.cmb` inspection finds five of six materials authored `vertex_lighting=1`, all
with ambient RGB 102 and diffuse RGB 127. Material 3 is the only unlit body material and has white
diffuse. Its VATR color buffer has size zero, so `HasColor=0` and PRIMARY is not body-wide vertex
color modulation. These facts falsify an unlit-material or hidden vertex-color explanation for the
remaining body-wide brightness observation.

## Host comparison and root cause

The native SDL3GPU CMB shader follows the recovered path: it uses the transformed skinned normal,
both packed directional slots, one actor ambient, and per-vertex saturation. No native-path
vertex-light discrepancy was found.

The optional unified CMB shader did not. Despite already carrying `uLitDif1`, `uLitDif2`, and
`uLightDir2` in its common UBO, lighting mode 2 dotted the model-space `nM` against only
`uLightDir`, normalized the direction again, and used only `uMatDiffuse`. It also pre-baked ambient
without the native packer's enabled-slot multiplicity. The grounded generic fix makes the unified
adapter copy the native light bank and makes the unified shader consume both slots with the
transformed normal. Focused tests lock both the shader inputs and the UBO adaptation.

## Falsifier / scope boundary

`gUnifiedRenderer` defaults to zero. Therefore a capture made on the default native CMB path cannot
be changed by this unified fix; such a capture falsifies this defect as the explanation for its
remaining BossFd2 residual. It does not falsify the fix itself: enabling unified CMB rendering is
the discriminator. No gain, material coefficient, BossFd2 special case, or secondary-UV change was
introduced.

## Integration evidence

The combined Clang build passes `mm_core`, `soh_core`, `lus_tests`, and
`zelda3d_app`. The focused unified renderer gate passes all eight selected
shader-template, CMB light-bank, and UBO-layout tests. Clang-tidy is clean for
the changed UBO adapter, SDL3GPU pass, and render-test translation units; the
unified shader translation unit still reports its pre-existing glslang
bitmask-enum analyzer diagnostic at line 53, outside this lighting change.
