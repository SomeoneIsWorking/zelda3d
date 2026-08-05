// dr_libs (dr_wav / dr_mp3 / dr_flac) compiled ONCE for the whole project.
//
// These are single-header libraries: the implementation lands in whichever translation unit
// defines DR_*_IMPLEMENTATION first. Both games did that in their own AudioSampleFactory.cpp, so
// each game core carried its own private copy — 266 dr_* symbols duplicated per side, measured by
// tools/core_overlap.py and recorded as claim C052. They are third-party audio decoders; they were
// never game code, and nothing about them differs between Ocarina of Time and Majora's Mask.
//
// So this file is the implementation, and the two AudioSampleFactory.cpp files now include the
// headers for their DECLARATIONS only. Both link against zelda3d_shared and resolve here.
//
// WHY C++ AND NOT C. The headers wrap themselves in extern "C", so the symbols have C linkage
// either way and a .c file would work. Compiling as C++ is deliberate anyway: that is exactly how
// this code was compiled before the hoist, so the generated code is unchanged and the move cannot
// alter decoder behaviour. The only thing that changes is how many copies exist.

#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>

#define DR_MP3_IMPLEMENTATION
#include <dr_mp3.h>

#define DR_FLAC_IMPLEMENTATION
#include <dr_flac.h>
