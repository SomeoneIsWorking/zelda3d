#pragma once

namespace Fast {

enum class Zelda3DTextureFilter {
    Nearest,
    Linear,
};

enum class Zelda3DMipmapFilter {
    None,
    Nearest,
    Linear,
};

struct Zelda3DSamplerFilter {
    Zelda3DTextureFilter minification;
    Zelda3DTextureFilter magnification;
    Zelda3DMipmapFilter mipmap;
};

// CMB texture bindings store the OpenGL ES sampler enums verbatim. Keep their
// minification and mip selection separate: GL_LINEAR (0x2601) does not sample
// mipmaps, while GL_LINEAR_MIPMAP_NEAREST (0x2701) and
// GL_LINEAR_MIPMAP_LINEAR (0x2703) select different mip filters.
constexpr Zelda3DSamplerFilter ResolveZelda3DSamplerFilter(unsigned minFilter, unsigned magFilter) {
    Zelda3DSamplerFilter result{
        minFilter == 0x2600 || minFilter == 0x2700 || minFilter == 0x2702 ? Zelda3DTextureFilter::Nearest
                                                                          : Zelda3DTextureFilter::Linear,
        magFilter == 0x2600 ? Zelda3DTextureFilter::Nearest : Zelda3DTextureFilter::Linear,
        Zelda3DMipmapFilter::None,
    };
    if (minFilter == 0x2700 || minFilter == 0x2701) {
        result.mipmap = Zelda3DMipmapFilter::Nearest;
    } else if (minFilter == 0x2702 || minFilter == 0x2703) {
        result.mipmap = Zelda3DMipmapFilter::Linear;
    }
    return result;
}

} // namespace Fast
